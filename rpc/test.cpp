#include <iostream>

#include <uvw.hpp>

#include "client.hpp"
#include "server.hpp"


RPC_PROTOCOL(echo_protocol,
	std::string RPC_METHOD(say_hello_world_v1)();
	std::string RPC_METHOD(echo_v1)(std::string text);
	void RPC_METHOD(request_something_from_me)(int32_t n);
)

RPC_PROTOCOL(reverse_echo_protocol,
	std::string RPC_METHOD(say_good_bye)(std::string name);
)


class echo_impl: public rpc::duplex_impl<echo_impl, echo_protocol, reverse_echo_protocol> {
public:
	std::string say_hello_world_v1() {
		return "Hello, world!";
	}
	std::string echo_v1(std::string text) {
		return "[" + text + "]";
	}
	void request_something_from_me(int32_t n) {
		peer.say_good_bye(std::to_string(n) + "th human on the Earth") | [](std::string text) { std::cout << text << std::endl; };
	}
};

class reverse_echo_impl: public rpc::duplex_impl<reverse_echo_impl, reverse_echo_protocol, echo_protocol> {
public:
	rpc::promise<std::string> say_good_bye(std::string name) {
		return peer.echo_v1(name) | [](std::string text) {
			return "Good bye, " + text + "!";
		};
	}
};


int main() {
	auto loop = uvw::Loop::getDefault();

	rpc::server<echo_impl, reverse_echo_protocol> server;
	server.bind("localhost:1024");
	server.bind("./rpc.sock");

	rpc::client<echo_protocol, reverse_echo_impl> client("./rpc.sock");

	client->say_hello_world_v1() | [](std::string text) { std::cout << text << std::endl; };
	client->request_something_from_me(28);

	std::vector<std::shared_ptr<uvw::SignalHandle>> signals;
	for(int signum: {SIGINT, SIGHUP, SIGTERM}) {
		auto signal = loop->resource<uvw::SignalHandle>();
		signal->on<uvw::SignalEvent>([&](const uvw::SignalEvent& ev, uvw::SignalHandle&) {
			std::cerr << "Shutting down..." << std::endl;
			for(auto& signal: signals) {
				signal->stop();
				signal->close();
			}
			signals.clear();
			server.stop();
			client.stop();
		});
		signal->start(signum);
		signals.push_back(std::move(signal));
	}

	loop->run();

	return 0;
}
