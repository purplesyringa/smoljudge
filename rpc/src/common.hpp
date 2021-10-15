#ifndef RPC_COMMON_HPP
#define RPC_COMMON_HPP


#include <array>

#include "serialization.hpp"


namespace rpc {
	struct generic_hello {
		uint32_t hello_size;
		std::array<char, 4> magic;
	};
	RPC_DEFINE_SERIALIZE(generic_hello, hello_size, magic)


	struct client_hello: public generic_hello {
		std::string requested_server_protocol_name;
		std::string advertised_client_protocol_name;
		std::vector<std::pair<std::string, std::string>> requested_server_methods;
		std::vector<std::pair<std::string, std::string>> advertised_client_methods;
	};
	RPC_DEFINE_SERIALIZE(client_hello, hello_size, magic, requested_server_protocol_name, advertised_client_protocol_name, requested_server_methods, advertised_client_methods)


	struct server_hello: public generic_hello {
		std::string error_message;
		std::vector<int32_t> method_ids;
	};
	RPC_DEFINE_SERIALIZE(server_hello, hello_size, magic, error_message, method_ids)


	struct rpc_message {
		uint32_t message_size;
		int32_t method_id;
		uint64_t message_id;
		std::vector<std::byte> args;
	};
	RPC_DEFINE_SERIALIZE(rpc_message, message_size, method_id, message_id, args)
};


#endif
