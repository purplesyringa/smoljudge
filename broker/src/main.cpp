#include <filesystem>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>
#include <uvw.hpp>

#include "invoker/protocol.hpp"
#include "rpc/server.hpp"

#include "protocol.hpp"
#include "queue.hpp"



class broker_impl: public rpc::duplex_impl<broker_impl, broker_protocol, invoker_protocol> {
public:
};



int main(int argc, char** argv) {
	if(argc != 2) {
		std::cerr << "Usage: " << argv[0] << " <path_to_config>" << std::endl;
		return 1;
	}


	std::filesystem::path config_path;
	try {
		config_path = std::filesystem::absolute(std::filesystem::path(argv[1]));
		std::filesystem::current_path(config_path.parent_path());
	} catch(std::filesystem::filesystem_error&) {
		std::cerr << "Could not open configuration file at " << argv[1] << std::endl;
		return 1;
	}

	std::cerr << "Working directory: " << config_path.parent_path() << std::endl;


	// Parse config
	nlohmann::json config;
	{
		std::ifstream fin(config_path.c_str());
		if(!fin) {
			std::cerr << "Could not open configuration file at " << argv[1] << std::endl;
			return 1;
		}
		fin >> config;
		char c;
		if(fin >> c) {
			std::cerr << "The configuration file contains excess data" << std::endl;
			return 1;
		}
	}


	std::string s = "print('hello, world!')";
	std::vector<std::byte> b(s.size());
	std::transform(s.begin(), s.end(), b.begin(), [](char c) { return (std::byte)c; });

	broker::queue q;
	q.add_submission(broker::pending_addition_submission{1, 2, {}});

	return 0;


	// Start server
	auto loop = uvw::Loop::getDefault();

	rpc::server<broker_impl, invoker_protocol> server;
	for(const std::string& address: config.at("listen")) {
		server.bind(address);
	}


	// Handle SIGINT, SIHUP, SIGTERM
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
		});
		signal->start(signum);
		signals.push_back(std::move(signal));
	}


	loop->run();


	return 0;
}
