#include <iostream>
#include <filesystem>

#include <uvw.hpp>

#include "async.hpp"
#include "common.hpp"
#include "server.hpp"


namespace rpc {
	generic_server::server_client::server_client(generic_server& server, size_t client_id, std::shared_ptr<generic_socket> sock): server(server), client_id(client_id), server_impl_object(nullptr), sock(std::move(sock)) {
		server_impl_object = server.server_impl_factory(std::make_unique<client_invoker>(*this));
	}

	generic_server::server_client::~server_client() {
		server.server_impl_deleter(server_impl_object);
	}


	void generic_server::server_client::stop() {
		sock->stop();
	}

	void generic_server::server_client::on_incoming_handshake(tcb::span<const std::byte> span) {
		client_hello hello = deserialize<client_hello>(span);
		if(hello.magic != std::array{'S', 'M', 'O', 'L'}) {
			sock->stop();
			return;
		}

		if(hello.requested_server_protocol_name != server.server_impl.protocol_name) {
			report_handshake_error(std::string("This server only supports ") + server.server_impl.protocol_name + " protocol");
			return;
		}

		if(hello.advertised_client_protocol_name != server.client_protocol.name) {
			report_handshake_error(std::string("This server requires the client to support ") + server.client_protocol.name + " protocol");
			return;
		}

		std::map<std::string, std::pair<std::string, int32_t>> advertised_client_methods;
		for(int32_t i = 0; i < hello.advertised_client_methods.size(); i++) {
			auto [method_name, method_signature] = std::move(hello.advertised_client_methods[i]);
			advertised_client_methods.insert({std::move(method_name), {std::move(method_signature), i}});
		}
		for(auto& method: server.client_protocol.methods) {
			if(!advertised_client_methods.count(method.name)) {
				report_handshake_error(std::string("The client method ") + method.name + " is not supported");
				return;
			}
			auto extracted = advertised_client_methods.extract(method.name);
			auto& [method_signature, method_id] = extracted.mapped();
			if(method.signature != method_signature) {
				report_handshake_error(std::string("The client method ") + method.name + " has mismatching signature. Expected: " + method.signature + ", present: " + method_signature);
				return;
			}
			client_ids_of_methods.emplace(std::move(extracted.key()), method_id);
		}

		server_hello reply;
		reply.hello_size = 0;
		reply.magic = {'s', 'm', 'o', 'l'};
		for(auto [method_name, method_signature]: hello.requested_server_methods) {
			if(!server.server_method_name_to_id.count(method_name)) {
				report_handshake_error("The server method " + method_name + " is not supported");
				return;
			}
			auto method_id = server.server_method_name_to_id[method_name];
			if(server.server_impl.methods[method_id].signature != method_signature) {
				report_handshake_error("The server method " + method_name + " has mismatching signature. Expected: " + method_signature + ", present: " + server.server_impl.methods[method_id].signature);
				return;
			}
			reply.method_ids.push_back(method_id);
		}

		reply.hello_size = serialize(reply).size();
		sock->write(serialize(reply));

		std::cerr << "Handshake with #" << client_id << " is now established" << std::endl;
	}


	void generic_server::server_client::report_handshake_error(const std::string& text) {
		server_hello reply;
		reply.hello_size = 0;
		reply.magic = {'s', 'm', 'o', 'l'};
		reply.error_message = text;
		reply.hello_size = serialize(reply).size();
		sock->write(serialize(reply));
		stop();
	}


	void generic_server::server_client::on_message(rpc_message&& message) {
		if(message.method_id == -1) {
			auto it = promises.find(message.message_id);
			if(it == promises.end()) {
				std::cerr << "Error on #" << client_id << ": response to message #" << message.message_id << ": no corresponding request or double response" << std::endl;
				return;
			}
			promises.extract(it).mapped().set(message.args);
		} else if(message.method_id == -2) {
			std::cerr << "Error on #" << client_id << ": message #" << message.message_id << ": " << deserialize<std::string>(message.args) << std::endl;
		} else if(0 <= message.method_id && message.method_id < server.server_impl.methods.size()) {
			server.server_impl.methods[message.method_id].fn(server_impl_object, message.args) | [sock = sock, message_id = message.message_id](std::vector<std::byte> result) {
				sock->reply(message_id, std::move(result));
			};
		} else {
			sock->report_error(message.message_id, "Unknown method");
		}
	}


	promise<std::vector<std::byte>> generic_server::server_client::invoke(const char* method_name, std::vector<std::byte>&& args) {
		uint64_t message_id = next_message_id++;
		sock->invoke(client_ids_of_methods.at(method_name), message_id, std::move(args));
		promise<std::vector<std::byte>> prom;
		promises.emplace(message_id, prom);
		return prom;
	}



	generic_server::client_invoker::client_invoker(server_client& client): client(client) {
	}

	promise<std::vector<std::byte>> generic_server::client_invoker::invoke(const char* method_name, std::vector<std::byte>&& args) {
		return client.invoke(method_name, std::move(args));
	}



	generic_server::generic_server(generic_impl server_impl_, generic_protocol client_protocol, void* (*server_impl_factory)(std::unique_ptr<generic_peer_invoker>&&), void (*server_impl_deleter)(void*)): server_impl(std::move(server_impl_)), client_protocol(std::move(client_protocol)), server_impl_factory(server_impl_factory), server_impl_deleter(server_impl_deleter) {
 		for(int32_t i = 0; i < server_impl.methods.size(); i++) {
			server_method_name_to_id[server_impl.methods[i].name] = i;
		}
	}

	generic_server::~generic_server() {
		stop();
	}


	void generic_server::stop() {
		for(const std::string& path: unix_domain_sockets) {
			std::filesystem::remove(path);
		}
		unix_domain_sockets.clear();
		for(auto close: server_stop_methods) {
			close();
		}
		server_stop_methods.clear();
	}


	template<typename Handle, typename Address> void generic_server::_bind_impl(const std::string& text_address, Address&& address) {
		auto loop = uvw::Loop::getDefault();
		auto server = loop->resource<Handle>();

		server->template on<uvw::ErrorEvent>([text_address](const uvw::ErrorEvent& ev, Handle&) {
			std::cerr << "Listener failure on " << text_address << ": " << ev.what() << std::endl;
		});

		server->template on<uvw::ListenEvent>([this, text_address](const uvw::ListenEvent&, Handle& server) {
			std::shared_ptr<Handle> client = server.loop().template resource<Handle>();

			size_t client_id = n_clients++;

			auto it = clients.emplace(clients.begin(), *this, client_id, std::make_shared<socket<Handle>>(client, true, [client](rpc_message&& message) {
				(*client->template data<server_client*>())->on_message(std::move(message));
			}, [client](tcb::span<const std::byte> span) {
				(*client->template data<server_client*>())->on_incoming_handshake(span);
			}));

			client->data(std::make_shared<server_client*>(&*it));

			client->template once<uvw::CloseEvent>([this, it](const uvw::CloseEvent&, Handle&) {
				clients.erase(it);
			});

			server.accept(*client);
			client->read();
		});

		server->bind(address);
		server->listen();

		server_stop_methods.push_back([server]() {
			server->stop();
			server->close();
		});
	}


	void generic_server::bind(std::string address) {
		std::cerr << "Listening on address " << address << std::endl;

		if(address[0] == '/' || (address[0] == '.' && address[1] == '/')) {
			// Path to UNIX domain socket
			if(std::filesystem::exists(address)) {
				std::cerr << "Listener failure on " << address << ": File exists" << std::endl;
			} else {
				_bind_impl<uvw::PipeHandle>(address, address);
				unix_domain_sockets.push_back(address);
			}
		} else {
			// IPv4 or IPv6
			auto i = address.rfind(':');
			if(i == std::string::npos) {
				throw std::runtime_error("Address must end with a colon followed by a port number");
			}

			auto loop = uvw::Loop::getDefault();
			auto getaddrinfo = loop->resource<uvw::GetAddrInfoReq>();
			auto addrinfo_result = getaddrinfo->addrInfoSync(address.substr(0, i), address.substr(i + 1));
			if(!addrinfo_result.first) {
				throw std::runtime_error("Could not resolve name");
			}
			struct addrinfo* addrinfo = addrinfo_result.second.get();
			_bind_impl<uvw::TCPHandle>(address, *addrinfo->ai_addr);
		}
	}
}
