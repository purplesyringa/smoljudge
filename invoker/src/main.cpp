#include <filesystem>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>
#include <uvw.hpp>

#include "broker/protocol.hpp"
#include "rpc/client.hpp"

#include "protocol.hpp"



class invoker_impl: public rpc::duplex_impl<invoker_impl, invoker_protocol, broker_protocol> {
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


	// Start client
	auto loop = uvw::Loop::getDefault();

	rpc::client<broker_protocol, invoker_impl> client(config.at("broker").get<std::string>());


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
			client.stop();
		});
		signal->start(signum);
		signals.push_back(std::move(signal));
	}


	loop->run();


	return 0;
}
