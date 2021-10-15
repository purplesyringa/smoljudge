#include <iostream>
#include <filesystem>

#include <uvw.hpp>

#include "client.hpp"
#include "serialization.hpp"


namespace rpc {
	generic_client::generic_client(generic_protocol server_protocol, generic_impl client_impl, std::string address_): server_text_address(std::move(address_)), server_protocol(std::move(server_protocol)), client_impl(std::move(client_impl)) {
		std::cerr << "Connecting to address " << server_text_address << std::endl;

		if(server_text_address[0] == '/' || (server_text_address[0] == '.' && server_text_address[1] == '/')) {
			// Path to UNIX domain socket
			_do_connect = [this]() {
				_connect_impl<uvw::PipeHandle>(server_text_address);
			};
		} else {
			// IPv4 or IPv6
			auto i = server_text_address.rfind(':');
			if(i == std::string::npos) {
				throw std::runtime_error("Address must end with a colon followed by a port number");
			}

			auto loop = uvw::Loop::getDefault();
			auto getaddrinfo = loop->resource<uvw::GetAddrInfoReq>();
			auto addrinfo_result = getaddrinfo->addrInfoSync(server_text_address.substr(0, i), server_text_address.substr(i + 1));
			if(!addrinfo_result.first) {
				throw std::runtime_error("Could not resolve name");
			}
			struct addrinfo* addrinfo = addrinfo_result.second.get();
			sockaddr_storage address = *reinterpret_cast<sockaddr_storage*>(addrinfo->ai_addr);
			_do_connect = [this, address]() {
				_connect_impl<uvw::TCPHandle>(reinterpret_cast<const sockaddr&>(address));
			};
		}

		_do_connect();
	}


	generic_client::~generic_client() {
		stop();
	}


	void generic_client::on_incoming_handshake(tcb::span<const std::byte> span) {
		// This method attempts to reconnect on handshake failure or protocol violation. Whether this is a wise idea or
		// it is better to fail fast is yet to be determined.
		server_hello hello = deserialize<server_hello>(span);

		if(hello.magic != std::array{'s', 'm', 'o', 'l'}) {
			std::cerr << "Client failure on " << server_text_address << ": invalid magic" << std::endl;
			reconnect(true);
			return;
		}

		if(!hello.error_message.empty()) {
			std::cerr << "Client failure on " << server_text_address << ": " << hello.error_message << std::endl;
			reconnect(true);
			return;
		}

		if(hello.method_ids.size() != server_protocol.methods.size()) {
			std::cerr << "Client failure on " << server_text_address << ": The requested method count and the returned method ID count differs" << std::endl;
			reconnect(true);
			return;
		}
		for(size_t i = 0; i < server_protocol.methods.size(); i++) {
			server_ids_of_methods[server_protocol.methods[i].name] = hello.method_ids[i];
		}

		std::cerr << "Handshake with " << server_text_address << " is now established" << std::endl;

		for(pending_message& pending: pending_messages) {
			rpc_message message;
			message.message_size = 0;
			message.method_id = server_ids_of_methods.at(pending.method_name);
			message.message_id = pending.message_id;
			message.args = std::move(pending.args);
			message.message_size = serialize(message).size();
			sock->write(serialize(message));
		}
		pending_messages.clear();
	}


	void generic_client::on_message(rpc_message&& message) {
		if(message.method_id == -1) {
			auto it = promises.find(message.message_id);
			if(it == promises.end()) {
				std::cerr << "Client failure on " << server_text_address << ": Response to message #" << message.message_id << ": no corresponding request or double response" << std::endl;
				return;
			}
			promises.extract(it).mapped().set(message.args);
		} else if(message.method_id == -2) {
			std::cerr << "Client failure on " << server_text_address << ": Message #" << message.message_id << ": " << deserialize<std::string>(message.args) << std::endl;
		} else if(0 <= message.method_id && message.method_id < client_impl.methods.size()) {
			client_impl.methods[message.method_id].fn(client_impl_object, message.args) | [sock = sock, message_id = message.message_id](std::vector<std::byte> result) {
				sock->reply(message_id, std::move(result));
			};
		} else {
			sock->report_error(message.message_id, "Unknown method");
		}
	}


	void generic_client::stop() {
		is_active = false;
		if(sock) {
			sock->stop();
			sock.reset();
		}
		if(timer) {
			timer->close();
			timer = nullptr;
		}
	}


	void generic_client::reconnect(bool due_to_failure) {
		if(due_to_failure) {
			n_failures++;
		} else {
			n_failures = 0;
		}

		if(sock) {
			sock->stop();
			sock.reset();
		}

		if(!is_active) {
			return;
		}

		std::cerr << "reconnecting..." << std::endl;

		auto loop = uvw::Loop::getDefault();
		timer = loop->template resource<uvw::TimerHandle>().get();

		timer->template on<uvw::TimerEvent>([this](const uvw::TimerEvent&, uvw::TimerHandle& timer) {
			timer.close();
			this->timer = nullptr;
			_do_connect();
		});

		int n_seconds;
		if(n_failures == 0) {
			n_seconds = 0;
		} else if(n_failures <= 3) {
			n_seconds = 1;
		} else {
			n_seconds = 1;
			for(int i = 0; i < std::min(6, n_failures - 3); i++) {
				n_seconds *= 2;
			}
		}
		timer->start(std::chrono::seconds{n_seconds}, std::chrono::seconds{0});
	}


	promise<std::vector<std::byte>> generic_client::invoke(const char* method_name, std::vector<std::byte>&& args) {
		uint64_t message_id = next_message_id++;
		if(!sock || !sock->handshake_finished()) {
			pending_messages.push_back({method_name, message_id, std::move(args)});
		} else {
			sock->invoke(server_ids_of_methods.at(method_name), message_id, std::move(args));
		}
		promise<std::vector<std::byte>> prom;
		promises.emplace(message_id, prom);
		return prom;
	}


	template<typename Handle, typename Address> void generic_client::_connect_impl(Address&& address) {
		auto loop = uvw::Loop::getDefault();
		auto client = loop->resource<Handle>();

		client->template on<uvw::ConnectEvent>([this](const uvw::ConnectEvent&, Handle& client) {
			client_hello hello;
			hello.hello_size = 0;
			hello.magic = {'S', 'M', 'O', 'L'};
			hello.requested_server_protocol_name = server_protocol.name;
			hello.advertised_client_protocol_name = client_impl.protocol_name;
			for(auto spec: server_protocol.methods) {
				hello.requested_server_methods.push_back({spec.name, spec.signature});
			}
			for(auto spec: client_impl.methods) {
				hello.advertised_client_methods.push_back({spec.name, spec.signature});
			}
			hello.hello_size = serialize(hello).size();
			sock->write(serialize(hello));
		});

		sock = std::make_shared<socket<Handle>>(client, false, [this](rpc_message&& message) {
			on_message(std::move(message));
		}, [this](tcb::span<const std::byte> span) {
			on_incoming_handshake(span);
		});

		client->template once<uvw::CloseEvent>([this](const uvw::CloseEvent&, Handle& client) {
			std::cerr << "Client failure on " << server_text_address << ": closed" << std::endl;
			reconnect(true);
		});

		client->connect(address);
	}
}
