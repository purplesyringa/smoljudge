#ifndef RPC_REFLECTION_HPP
#define RPC_REFLECTION_HPP


#include <tuple>
#include <type_traits>
#include <vector>

#include <tcb/span.hpp>

#include "async.hpp"
#include "serialization.hpp"


#define RPC_PROTOCOL(protocol_name, body) \
	template<typename Strategy> struct _group_##protocol_name { \
		Strategy strategy; \
		template<typename... Args> _group_##protocol_name(Args&&... args): strategy(std::forward<Args>(args)...) { \
		} \
		body \
	}; \
	struct protocol_name: public ::rpc::reflective_protocol<protocol_name, _group_##protocol_name> { \
		static constexpr const char* name = #protocol_name; \
	};


#define RPC_METHOD(method_name) \
	/* type */ * _retval_##method_name = nullptr; \
	using _return_##method_name = std::remove_pointer_t<decltype(_retval_##method_name)>; \
	RPC_METHOD_IMPL LPAREN method_name, _return_##method_name, RPC_METHOD_ARGS /* (signature) */

#define RPC_METHOD_ARGS(...) __VA_ARGS__ RPAREN


#define RPC_METHOD_IMPL(method_name, return_type, ...) \
	return_type (*_signature_##method_name)(__VA_ARGS__) = nullptr; \
	typename Strategy::template announcement<decltype(_signature_##method_name)> _announcement_##method_name{#method_name, [](auto&& opt) { return &std::decay_t<decltype(opt)>::type::method_name; }}; \
	template<typename... Args> decltype(auto) method_name(Args&&... args) { \
		return strategy.template invoke<decltype(_signature_##method_name)>(#method_name, std::forward<Args>(args)...); \
	}


#define LPAREN (
#define RPAREN )


namespace rpc {
	namespace reflection {
		template<typename T> struct fn_traits {
		};
		template<typename ReturnType, typename... Signature> struct fn_traits<ReturnType(*)(Signature...)> {
			using args_tuple = std::tuple<Signature...>;
			using return_type = ReturnType;
			inline static std::string args_text = join_strings(", ", {stringify_type<Signature>()...});
			inline static std::string return_text = stringify_type<ReturnType>();
			template<typename Invoker, typename... Args> static decltype(auto) invoke(Invoker&& invoker, const char* method_name, Args&&... args) {
				return invoker.template invoke<ReturnType>(method_name, static_cast<Signature>(args)...);
			}
		};
		template<typename Class, typename ReturnType, typename... Signature> struct fn_traits<ReturnType(Class::*)(Signature...)> {
			using args_tuple = std::tuple<Signature...>;
			using return_type = ReturnType;
		};


		struct method {
			const char* name;
			std::string signature;
		};

		struct method_impl {
			const char* name;
			std::string signature;
			std::function<promise<std::vector<std::byte>>(void*, const std::vector<std::byte>&)> fn;
		};
	}


	struct generic_protocol {
		const char* name;
		tcb::span<const reflection::method> methods;
	};

	struct generic_impl {
		const char* protocol_name;
		tcb::span<const reflection::method_impl> methods;
	};


	template<typename Protocol, template<typename Strategy> typename Group> struct reflective_protocol {
		template<typename Strategy> using group = Group<Strategy>;


		template<typename Invoker> struct proxy_strategy {
			Invoker _invoker;
			proxy_strategy(Invoker&& invoker): _invoker(std::move(invoker)) {
			}
			template<typename Signature, typename... Args> decltype(auto) invoke(const char* method_name, Args&&... args) {
				return reflection::fn_traits<Signature>::invoke(_invoker, method_name, std::forward<Args>(args)...);
			}
			template<typename Signature> struct announcement {
				template<typename Getter> inline announcement(const char* method_name, Getter&&) {
				}
			};
		};

		template<typename Invoker> using proxy = Group<proxy_strategy<Invoker>>;


		static inline struct reflection_t {
			std::vector<reflection::method> methods;
			reflection_t() {
				Group<strategy>{};
			}
			struct strategy {
				template<typename Signature> struct announcement {
					template<typename Getter> inline announcement(const char* method_name, Getter&&) {
						_reflection.methods.push_back({method_name, stringify_type<std::remove_pointer_t<Signature>>()});
					}
				};
			};
		} _reflection;

		static generic_protocol to_generic_protocol() {
			return {Protocol::name, {_reflection.methods.data(), _reflection.methods.size()}};
		}
	};


	class generic_peer_invoker {
	public:
		virtual ~generic_peer_invoker() = default;
		virtual promise<std::vector<std::byte>> invoke(const char* method_name, std::vector<std::byte>&& args) = 0;
	};


	struct peer_proxy_invoker {
		std::unique_ptr<generic_peer_invoker> invoker;

		peer_proxy_invoker(std::unique_ptr<generic_peer_invoker>&& invoker): invoker(std::move(invoker)) {
		}

		template<typename ReturnType, typename... Args> promise<ReturnType> invoke(const char* method_name, Args&&... args) {
			return invoker->invoke(method_name, serialize(std::tuple<Args...>{std::forward<Args>(args)...})) | [](const std::vector<std::byte>& data) {
				return deserialize<ReturnType>(data);
			};
		}
	};


	template<typename SelfImpl, typename SelfProtocol, typename PeerProtocol> class duplex_impl {
		static inline struct reflection_t {
			std::vector<reflection::method_impl> methods;
			reflection_t() {
				typename SelfProtocol::template group<strategy>{};
			}
			struct strategy {
				struct impl_container {
					using type = SelfImpl;
				};
				template<typename Signature> struct announcement {
					template<typename Getter> inline announcement(const char* method_name, Getter&& getter) {
						auto method = getter(impl_container{});
						_reflection.methods.push_back({method_name, stringify_type<std::remove_pointer_t<Signature>>(), [method](void* impl_ptr, const std::vector<std::byte>& args) -> promise<std::vector<std::byte>> {
							SelfImpl& self_impl = *static_cast<SelfImpl*>(impl_ptr);
							auto get_result = [&]() -> decltype(auto) {
								return std::apply([&self_impl, method](auto&&... args) -> decltype(auto) {
									return (self_impl.*method)(std::forward<decltype(args)>(args)...);
								}, deserialize<typename reflection::fn_traits<decltype(method)>::args_tuple>(args));
							};
							if constexpr(std::is_same_v<decltype(get_result()), void>) {
								get_result();
								return to_promise(std::vector<std::byte>{});
							} else {
								return to_promise(get_result()) | [](auto value) {
									return serialize(std::forward<decltype(value)>(value));
								};
							}
						}});
					}
				};
			};
		} _reflection;


	public:
		typename PeerProtocol::template proxy<peer_proxy_invoker> peer;

		duplex_impl(std::unique_ptr<generic_peer_invoker>&& invoker): peer(peer_proxy_invoker{std::move(invoker)}) {
		}

		static generic_impl to_generic_impl() {
			return {SelfProtocol::name, {_reflection.methods.data(), _reflection.methods.size()}};
		}
	};
}


#endif
