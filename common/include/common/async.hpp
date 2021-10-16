#ifndef COMMON_ASYNC_HPP
#define COMMON_ASYNC_HPP


#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <type_traits>
#include <variant>


namespace async {
	template<typename F> class unique_function: private std::function<F> {
		template<class Fun> struct hack {
			std::optional<Fun> f;
			hack(Fun p): f(std::move(p)) {
			}
			hack(hack&&) = default;
			hack(hack const&) {
				throw -1;
			}
			hack& operator=(hack&&) = default;
			hack& operator=(hack const&) {
				throw -1;
			}
			template<typename... Args> decltype(auto) operator()(Args&&... args) {
				return (*f)(std::forward<Args>(args)...);
			}
		};

	public:
		template<class Fun> unique_function(Fun&& fun): std::function<F>(hack<std::remove_cvref_t<Fun>>(std::move(fun))) {
		}
		unique_function() = default;
		unique_function(std::nullptr_t): std::function<F>(nullptr) {
		}
		unique_function(unique_function const&) = delete;
		unique_function(unique_function&&) = default;
		unique_function& operator=(unique_function const&) = delete;
		unique_function& operator=(unique_function&&) = default;
		using std::function<F>::operator();
		using std::function<F>::operator bool;
	};


	template<typename T> struct simple_fn_traits {
	};
	template<typename ReturnType, typename... Args> struct simple_fn_traits<ReturnType(*)(Args...)> {
		using return_type = ReturnType;
		using args_tuple = std::tuple<Args...>;
	};
	template<typename T> using fn_traits = simple_fn_traits<decltype(+std::declval<T>())>;


	template<size_t I, typename Promise, typename... Catch> void exception_handler_impl(Promise& result, std::exception& ex, std::tuple<Catch...>& catch_handlers) {
		if constexpr(I == sizeof...(Catch)) {
			try {
				throw ex;
			} catch(...) {
				std::terminate();
			}
		} else {
			auto& catch_handler = std::get<I>(catch_handlers);
			using args_tuple = typename fn_traits<decltype(catch_handler)>::args_tuple;
			static_assert(std::tuple_size_v<args_tuple> == 1, "Exception handler must have exactly one argument");
			using Exception = std::remove_cvref_t<std::tuple_element_t<0, args_tuple>>;
			Exception* caught_ex = nullptr;
			try {
				caught_ex = dynamic_cast<Exception*>(&ex);
			} catch(std::bad_cast&) {
				exception_handler_impl<I + 1>(result, ex, catch_handlers);
				return;
			}
			result->set(std::move(catch_handler)(*caught_ex));
		}
	}


	template<typename Else, typename... Catch> struct exception_handler {
		Else else_handler;
		std::tuple<Catch...> catch_handlers;
	public:
		template<typename T> using return_type = std::common_type_t<typename std::conditional_t<std::is_same_v<T, void>, std::invoke_result<Else>, std::invoke_result<Else, T>>::type, typename fn_traits<Catch>::return_type...>;
		exception_handler(Else else_handler, std::tuple<Catch...> catch_handlers): else_handler(std::move(else_handler)), catch_handlers(std::move(catch_handlers)) {
		}
		template<typename C> auto catch_(C catch_handler) && {
			return exception_handler<Else, Catch..., C>{else_handler, std::tuple_cat(std::move(catch_handlers), std::tuple<C>{std::move(catch_handler)})};
		}
		template<typename T, typename Promise> void handle(Promise& result, T* value, std::exception_ptr ex) && {
			if(ex) {
				try {
					std::rethrow_exception(ex);
				} catch(std::exception& real_ex) {
					exception_handler_impl<0>(result, real_ex, catch_handlers);
				}
			} else {
				result.set(else_handler(std::move(*value)));
			}
		}
		template<typename Promise> void handle(Promise& result, std::exception_ptr ex) && {
			if(ex) {
				try {
					std::rethrow_exception(ex);
				} catch(std::exception& real_ex) {
					exception_handler_impl<0>(result, real_ex, catch_handlers);
				}
			} else {
				result->set(else_handler());
			}
		}
	};


	template<typename... Catch> struct exception_handler<std::monostate, Catch...> {
		std::tuple<Catch...> catch_handlers;
	public:
		template<typename T> using return_type = std::common_type_t<typename fn_traits<Catch>::return_type...>;
		exception_handler(std::monostate, std::tuple<Catch...> catch_handlers): catch_handlers(std::move(catch_handlers)) {
		}
		template<typename C> auto catch_(C catch_handler) && {
			return exception_handler<std::monostate, Catch..., C>{std::monostate{}, std::tuple_cat(std::move(catch_handlers), std::tuple<C>{std::move(catch_handler)})};
		}
		template<typename E> auto else_(E else_handler) && {
			return exception_handler<E, Catch...>{else_handler, std::move(catch_handlers)};
		}
	};


	struct create_resolved {
	};


	template<typename T> class _promise_impl {
		unique_function<void(T*, std::exception_ptr)> callback;
		std::optional<T> value;
		std::exception_ptr ex;

	public:
		_promise_impl() {
		}
		_promise_impl(create_resolved, const T& value): value(value) {
		}
		_promise_impl(create_resolved, T&& value): value(std::move(value)) {
		}

		_promise_impl(const _promise_impl& other) = delete;
		_promise_impl(_promise_impl&& other) = default;
		_promise_impl& operator=(const _promise_impl& other) = delete;
		_promise_impl& operator=(_promise_impl&& other) = default;

		void set(T&& new_value) {
			if(callback) {
				callback(&new_value, nullptr);
			} else {
				value = new_value;
			}
		}

		void throw_(std::exception_ptr new_ex) {
			if(callback) {
				callback(nullptr, new_ex);
			} else {
				ex = new_ex;
			}
		}

		template<typename F> auto operator|(F&& transform) {
			using R = std::remove_cvref_t<decltype(std::declval<F>()(std::declval<T>()))>;
			auto result = std::make_shared<_promise_impl<R>>();
			callback = [transform = std::move(transform), result](T* value, std::exception_ptr ex) mutable {
				if(ex) {
					result->throw_(ex);
				} else {
					if constexpr(std::is_same_v<R, void>) {
						transform(std::move(*value));
						result->set();
					} else {
						result->set(transform(std::move(*value)));
					}
				}
			};
			if(value) {
				callback(&*value, nullptr);
			} else if(ex) {
				callback(nullptr, ex);
			}
			return result;
		}

		template<typename E, typename... Catch> auto operator|(exception_handler<E, Catch...>&& handler) {
			using R = typename std::decay_t<decltype(handler)>::template return_type<T>;
			auto result = std::make_shared<_promise_impl<R>>();
			callback = [handler = std::move(handler), result](T* value, std::exception_ptr ex) mutable {
				std::move(handler).handle(result, value, ex);
			};
			if(value) {
				callback(&*value, nullptr);
			} else if(ex) {
				callback(nullptr, ex);
			}
			return result;
		}
	};


	template<> class _promise_impl<void> {
		unique_function<void(std::exception_ptr)> callback;
		bool is_set = false;
		std::exception_ptr ex;

	public:
		_promise_impl() {
		}
		_promise_impl(create_resolved): callback(nullptr), is_set(true) {
		}

		_promise_impl(const _promise_impl& other) = delete;
		_promise_impl(_promise_impl&& other) = default;
		_promise_impl& operator=(const _promise_impl& other) = delete;
		_promise_impl& operator=(_promise_impl&& other) = default;

		void set() {
			if(callback) {
				callback(nullptr);
			} else {
				is_set = true;
			}
		}

		void throw_(std::exception_ptr new_ex) {
			if(callback) {
				callback(new_ex);
			} else {
				ex = new_ex;
			}
		}

		template<typename F> auto operator|(F&& transform) {
			using R = decltype(std::declval<F>()());
			auto result = std::make_shared<_promise_impl<R>>();
			callback = [transform = std::move(transform), result](std::exception_ptr ex) mutable {
				if(ex) {
					result->throw_(ex);
				} else {
					if constexpr(std::is_same_v<R, void>) {
						transform();
						result->set();
					} else {
						result->set(transform());
					}
				}
			};
			if(is_set) {
				callback(nullptr);
			} else if(ex) {
				callback(ex);
			}
			return result;
		}

		template<typename E, typename... Catch> auto operator|(exception_handler<E, Catch...>&& handler) {
			using R = typename std::decay_t<decltype(handler)>::template return_type<void>;
			auto result = std::make_shared<_promise_impl<R>>();
			callback = [handler = std::move(handler), result](std::exception_ptr ex) mutable {
				std::move(handler).handle(result, ex);
			};
			if(is_set) {
				callback(nullptr);
			} else if(ex) {
				callback(ex);
			}
			return result;
		}
	};


	template<typename T> class promise {
		std::shared_ptr<_promise_impl<T>> impl;
		promise(std::shared_ptr<_promise_impl<T>>&& impl): impl(std::move(impl)) {
		}

	public:
		promise(): impl(std::make_shared<_promise_impl<T>>()) {
		}

		void set(T&& value) {
			impl->set(std::move(value));
		}
		template<typename F> auto operator|(F&& transform) {
			return ::async::promise{*impl | std::forward<F>(transform)};
		}

		template<typename U> friend class promise;

		template<typename U> friend promise<U> to_promise(const U& value);
	};

	template<> class promise<void> {
		std::shared_ptr<_promise_impl<void>> impl;
		promise(std::shared_ptr<_promise_impl<void>>&& impl): impl(std::move(impl)) {
		}

	public:
		promise(): impl(std::make_shared<_promise_impl<void>>()) {
		}

		void set() {
			impl->set();
		}
		template<typename F> auto operator|(F&& transform) {
			return ::async::promise{*impl | std::forward<F>(transform)};
		}

		template<typename U> friend class promise;
	};


	template<typename F> auto catch_(F&& handler) {
		return exception_handler<std::monostate, F>{std::monostate{}, {std::forward<F>(handler)}};
	}


	template<typename T> promise<T> to_promise(const T& value) {
		return promise<T>{std::make_shared<_promise_impl<T>>(create_resolved{}, value)};
	}

	template<typename T> promise<T> to_promise(promise<T> prom) {
		return prom;
	}
}


#endif
