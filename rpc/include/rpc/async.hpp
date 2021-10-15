#ifndef RPC_ASYNC_HPP
#define RPC_ASYNC_HPP


#include <functional>
#include <memory>
#include <optional>
#include <type_traits>


namespace rpc {
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


	struct create_resolved {
	};


	template<typename T> class _promise_impl {
		unique_function<void(const T&)> callback;
		std::optional<T> value;

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

		void set(const T& new_value) {
			if(callback) {
				callback(new_value);
			} else {
				value = new_value;
			}
		}

		template<typename F> auto operator|(F&& transform) {
			using R = std::remove_cvref_t<decltype(std::declval<F>()(std::declval<T>()))>;
			auto result = std::make_shared<_promise_impl<R>>();
			callback = [transform = std::move(transform), result](const T& value) mutable {
				if constexpr(std::is_same_v<R, void>) {
					transform(std::move(value));
					result->set();
				} else {
					result->set(transform(std::move(value)));
				}
			};
			if(value) {
				callback(std::move(*value));
			}
			return result;
		}
	};


	template<> class _promise_impl<void> {
		unique_function<void(void)> callback;
		bool is_set = false;

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
				callback();
			} else {
				is_set = true;
			}
		}

		template<typename F> auto operator|(F&& transform) {
			using R = decltype(std::declval<F>()());
			auto result = std::make_shared<_promise_impl<R>>();
			callback = [transform = std::move(transform), result]() mutable {
				if constexpr(std::is_same_v<R, void>) {
					transform();
					result->set();
				} else {
					result->set(transform());
				}
			};
			if(is_set) {
				callback();
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

		void set(const T& value) {
			impl->set(value);
		}
		template<typename F> auto operator|(F&& transform) {
			return ::rpc::promise{*impl | transform};
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
			return ::rpc::promise{*impl | transform};
		}

		template<typename U> friend class promise;
	};


	template<typename T> promise<T> to_promise(const T& value) {
		return promise<T>{std::make_shared<_promise_impl<T>>(create_resolved{}, value)};
	}

	template<typename T> promise<T> to_promise(promise<T> prom) {
		return prom;
	}
}


#endif
