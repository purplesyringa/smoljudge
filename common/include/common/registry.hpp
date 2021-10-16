#ifndef COMMON_REGISTER_HPP
#define COMMON_REGISTER_HPP


#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

#include "common/async.hpp"
#include "rpc/client.hpp"


class registry {
public:
	registry(const std::filesystem::path& path);

	async::promise<void> store(const std::string& data_class, uint64_t id, const std::vector<std::byte>& data);
	async::promise<std::optional<std::vector<std::byte>>> retrieve(const std::string& data_class, uint64_t id);
};


#endif
