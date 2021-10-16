#include <filesystem>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>
#include <uvw.hpp>

#include "rpc/server.hpp"

#include "protocol.hpp"
#include "common/registry.hpp"


std::optional<registry> reg;


class registry_impl: public rpc::simplex_impl<registry_impl, registry_protocol> {
public:
	async::promise<bool> store(std::string data_class, uint64_t id, std::vector<std::byte> data) {
		return reg->store(data_class, id, data) | async::catch_([](std::runtime_error& ex) {
			return false;
		}).else_([]() {
			return true;
		});
	}

	async::promise<std::optional<std::vector<std::byte>>> retrieve(std::string data_class, uint64_t id) {
		return reg->retrieve(data_class, id);
	}
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


	reg.emplace(config["data_dir"].get<std::string>());


	// Start server
	auto loop = uvw::Loop::getDefault();

	rpc::server<registry_impl> server;
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
