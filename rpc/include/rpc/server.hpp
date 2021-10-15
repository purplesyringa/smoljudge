#ifndef RPC_SERVER_HPP
#define RPC_SERVER_HPP


#include <functional>
#include <list>
#include <map>
#include <string>
#include <vector>

#include <tcb/span.hpp>

#include "async.hpp"
#include "common.hpp"
#include "reflection.hpp"
#include "socket.hpp"


namespace rpc {
	class generic_server {
		class server_client {
			generic_server& server;
			size_t client_id;
			void* server_impl_object;

			std::map<std::string, int32_t> client_ids_of_methods;
			std::map<size_t, promise<std::vector<std::byte>>> promises;
			uint64_t next_message_id = 0;

			std::shared_ptr<generic_socket> sock;

		public:
			server_client(generic_server& server, size_t client_id, std::shared_ptr<generic_socket> sock);
			~server_client();

			void stop();
			void on_message(rpc_message&& message);
			void on_incoming_handshake(tcb::span<const std::byte> span);

			promise<std::vector<std::byte>> invoke(const char* method_name, std::vector<std::byte>&& args);

			void report_handshake_error(const std::string& text);
			void handle_message(const rpc_message& message);

			friend class generic_server;
		};


		class client_invoker: public generic_peer_invoker {
			server_client& client;

		public:
			client_invoker(server_client& client);
			virtual promise<std::vector<std::byte>> invoke(const char* method_name, std::vector<std::byte>&& args);
		};


		std::vector<std::string> unix_domain_sockets;
		std::list<server_client> clients;
		std::vector<std::function<void(void)>> server_stop_methods;
		size_t n_clients = 0;

		generic_impl server_impl;
		generic_protocol client_protocol;

		std::map<std::string, int32_t> server_method_name_to_id;

		void* (*server_impl_factory)(std::unique_ptr<generic_peer_invoker>&&);
		void (*server_impl_deleter)(void*);


		template<typename Handle, typename Address> void _bind_impl(const std::string& text_address, Address&& address);

	public:
		generic_server(generic_impl server_impl, generic_protocol client_protocol, void* (*server_impl_factory)(std::unique_ptr<generic_peer_invoker>&&), void (*server_impl_deleter)(void*));
		~generic_server();

		void stop();
		void bind(std::string address);
	};


	template<typename ServerImpl, typename ClientProtocol> class server: public generic_server {
		static void* _server_impl_factory(std::unique_ptr<generic_peer_invoker>&& invoker) {
			return new ServerImpl{std::move(invoker)};
		}
		static void _server_impl_deleter(void* server_impl) {
			delete static_cast<ServerImpl*>(server_impl);
		}
	public:
		server(): generic_server(ServerImpl::to_generic_impl(), ClientProtocol::to_generic_protocol(), &_server_impl_factory, &_server_impl_deleter) {
		}
	};
};


#endif
