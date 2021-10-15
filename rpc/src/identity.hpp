#ifndef RPC_IDENTITY_HPP
#define RPC_IDENTITY_HPP


#include <functional>
#include <string>
#include <vector>

#include <tcb/span.hpp>


namespace uvw {
	class TimerHandle;
}

namespace rpc {
	class client {
		// enum class reliability {
		// 	best_effort,
		// 	guaranteed_delivery
		// };


		std::function<void(void)> stop_impl, reconnect_impl;
		std::function<void(std::vector<std::byte>)> write_impl;
		bool is_connected = false;
		void* current_connection = nullptr;
		int n_failures = 0;
		uvw::TimerHandle* timer = nullptr;
		// std::vector<std::vector<std::byte>> pending_send_messages;

		template<typename Handle, typename Address> void _connect_impl(const std::string& text_address, Address&& address);

		// void on_data(tcb::span<const std::byte> span);
		void handshake();

	public:
		explicit client(std::string address);
		~client();

		void stop();
		void reconnect(bool due_to_failure = false);

		// bool send_message(std::vector<std::byte> message, std::function<void(std::vector<std::byte>)> = nullptr);
	};
};


#endif
