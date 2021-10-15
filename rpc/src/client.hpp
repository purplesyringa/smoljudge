#ifndef RPC_CLIENT_HPP
#define RPC_CLIENT_HPP


#include <functional>
#include <string>
#include <map>
#include <vector>

#include <tcb/span.hpp>

#include "async.hpp"
#include "common.hpp"
#include "reflection.hpp"
#include "socket.hpp"


namespace uvw {
	class TimerHandle;
}


namespace rpc {
	struct pending_message {
		const char* method_name;
		uint64_t message_id;
		std::vector<std::byte> args;
	};


	class generic_client {
		std::string server_text_address;
		std::shared_ptr<generic_socket> sock;

		std::function<void(void)> _do_connect;

		bool is_active = true;

		int n_failures = 0;
		uvw::TimerHandle* timer = nullptr;

		generic_protocol server_protocol;
		std::map<std::string, int32_t> server_ids_of_methods;
		std::map<size_t, promise<std::vector<std::byte>>> promises;
		uint64_t next_message_id = 0;

		generic_impl client_impl;

		std::vector<pending_message> pending_messages;

		template<typename Handle, typename Address> void _connect_impl(Address&& address);

		void on_incoming_handshake(tcb::span<const std::byte> span);
		void on_message(rpc_message&& message);

	protected:
		struct proxy_invoker {
			generic_client* client;

			template<typename ReturnType, typename... Args> promise<ReturnType> invoke(const char* method_name, Args&&... args) {
				return client->invoke(method_name, serialize(std::tuple<Args...>{std::forward<Args>(args)...})) | [](const std::vector<std::byte>& data) {
					return deserialize<ReturnType>(data);
				};
			}
		};

		friend struct proxy_invoker;

		void* client_impl_object;

	public:
		explicit generic_client(generic_protocol server_protocol, generic_impl client_impl, std::string address);
		~generic_client();

		void stop();
		void reconnect(bool due_to_failure = false);
		promise<std::vector<std::byte>> invoke(const char* method_name, std::vector<std::byte>&& args);
	};



	template<typename ServerProtocol, typename ClientImpl> class client: public generic_client {
		class server_invoker: public generic_peer_invoker {
			client& _client;

		public:
			server_invoker(client& _client): _client(_client) {
			}

			virtual promise<std::vector<std::byte>> invoke(const char* method_name, std::vector<std::byte>&& args) {
				return _client.invoke(method_name, std::move(args));
			}
		};


		typename ServerProtocol::template proxy<proxy_invoker> proxy;

	public:
		explicit client(std::string address): generic_client(ServerProtocol::to_generic_protocol(), ClientImpl::to_generic_impl(), std::move(address)), proxy(proxy_invoker{this}) {
			client_impl_object = new ClientImpl{static_cast<std::unique_ptr<generic_peer_invoker>>(std::make_unique<server_invoker>(*this))};
		}
		~client() {
			delete static_cast<ClientImpl*>(client_impl_object);
		}

		auto operator->() {
			return &proxy;
		}
	};
};


#endif
