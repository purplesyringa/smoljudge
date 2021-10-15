#ifndef RPC_SOCKET_HPP
#define RPC_SOCKET_HPP


#include <vector>
#include <cstddef>

#include "tcb/span.hpp"
#include "uvw.hpp"

#include "async.hpp"


namespace rpc {
	class generic_socket {
	protected:
		bool _is_open;
		bool _is_connected;
		bool _handshake_finished;
		size_t next_message_id;
		std::vector<std::byte> message_piece;
		std::function<void(rpc_message&&)> _on_message;
		std::function<void(tcb::span<const std::byte>)> _on_incoming_handshake;

	public:
		inline generic_socket(bool is_preconnected, std::function<void(rpc_message&&)> on_message, std::function<void(tcb::span<const std::byte>)> on_incoming_handshake): _is_open(true), _is_connected(is_preconnected), _handshake_finished(false), next_message_id(0), _on_message(on_message), _on_incoming_handshake(on_incoming_handshake) {
		}

		virtual ~generic_socket() = default;

		virtual void write(std::vector<std::byte> data) = 0;

		virtual void stop() = 0;

		inline bool is_open() const {
			return _is_open;
		}
		inline bool is_connected() const {
			return _is_connected;
		}
		inline bool handshake_finished() const {
			return _handshake_finished;
		}

		inline void reply(uint64_t message_id, std::vector<std::byte> response) {
			rpc_message message;
			message.message_size = 0;
			message.method_id = -1;
			message.message_id = message_id;
			message.args = std::move(response);
			message.message_size = serialize(message).size();
			write(serialize(message));
		}

		inline void report_error(uint64_t message_id, const std::string& text) {
			rpc_message message;
			message.message_size = 0;
			message.method_id = -2;
			message.message_id = message_id;
			message.args = serialize(text);
			message.message_size = serialize(message).size();
			write(serialize(message));
		}


		inline void invoke(int32_t method_id, uint64_t message_id, std::vector<std::byte> args) {
			rpc_message message;
			message.message_size = 0;
			message.method_id = method_id;
			message.message_id = message_id;
			message.args = std::move(args);
			message.message_size = serialize(message).size();
			write(serialize(message));
		}
	};


	template<typename Handle> class socket: public generic_socket {
		std::shared_ptr<Handle> handle;

		std::optional<typename Handle::template Connection<uvw::ConnectEvent>> connect_handler;
		typename Handle::template Connection<uvw::EndEvent> end_handler;
		typename Handle::template Connection<uvw::DataEvent> data_handler;
		typename Handle::template Connection<uvw::ErrorEvent> error_handler;

	public:
		socket(std::shared_ptr<Handle> handle_, bool is_preconnected, std::function<void(rpc_message&&)> on_message, std::function<void(tcb::span<const std::byte>)> on_incoming_handshake): generic_socket(is_preconnected, on_message, on_incoming_handshake), handle(std::move(handle_)) {
			if(!_is_connected) {
				connect_handler = handle->template on<uvw::ConnectEvent>([this](const uvw::ConnectEvent&, Handle& handle) {
					_is_connected = true;
					handle.read();
				});
			}
			end_handler = handle->template on<uvw::EndEvent>([this](const uvw::EndEvent&, Handle& handle) {
				_is_open = false;
				_is_connected = false;
				handle.close();
			});
			data_handler = handle->template on<uvw::DataEvent>([this](const uvw::DataEvent& ev, Handle& handle) {
				try {
					on_data(tcb::span<const std::byte>(reinterpret_cast<const std::byte*>(ev.data.get()), ev.length));
				} catch(std::exception& ex) {
					std::cerr << "Exception on socket on_data: " << ex.what() << std::endl;
					stop();
				}
			});
			error_handler = handle->template on<uvw::ErrorEvent>([this](const uvw::ErrorEvent&, Handle& handle) {
				_is_open = false;
				_is_connected = false;
				handle.close();
			});
		}

		~socket() {
			if(connect_handler) {
				handle->erase(*connect_handler);
			}
			handle->erase(end_handler);
			handle->erase(data_handler);
			handle->erase(error_handler);
		}


		void on_data(tcb::span<const std::byte> data) {
			message_piece.insert(message_piece.end(), data.begin(), data.end());

			if(!_handshake_finished) {
				if(message_piece.size() < 8) {
					return;
				}

				generic_hello hello_header = deserialize<generic_hello>(tcb::span{message_piece.data(), message_piece.data() + 8});

				if(
					std::tolower(static_cast<unsigned char>(hello_header.magic[0])) != 's' ||
					std::tolower(static_cast<unsigned char>(hello_header.magic[1])) != 'm' ||
					std::tolower(static_cast<unsigned char>(hello_header.magic[2])) != 'o' ||
					std::tolower(static_cast<unsigned char>(hello_header.magic[3])) != 'l'
				) {
					std::cerr << "Failure on socket: invalid magic" << std::endl;
					stop();
					return;
				}

				if(hello_header.hello_size > 8 * 1024) {
					std::cerr << "Failure on socket: too long hello" << std::endl;
					stop();
					return;
				}

				if(message_piece.size() < hello_header.hello_size) {
					return;
				}

				_on_incoming_handshake(tcb::span{message_piece.data(), message_piece.data() + hello_header.hello_size});
				message_piece.erase(message_piece.begin(), message_piece.begin() + hello_header.hello_size);
				_handshake_finished = true;
			}


			while(message_piece.size() >= 4) {
				uint32_t message_size = deserialize<uint32_t>(tcb::span{message_piece.data(), message_piece.data() + 4});
				if(message_piece.size() < message_size) {
					break;
				}

				rpc_message message = deserialize<rpc_message>(tcb::span{message_piece.data(), message_piece.data() + message_size});
				message_piece.erase(message_piece.begin(), message_piece.begin() + message_size);

				on_message(std::move(message));
			}
		}


		void on_message(rpc_message&& message) {
			_on_message(std::move(message));
		}


		virtual void write(std::vector<std::byte> data) {
			if(!_is_connected) {
				throw std::runtime_error("Socket not connected");
			}
			std::byte* ptr = data.data();
			size_t size = data.size();
			auto deleter = [data = std::move(data)](char*) {
			};
			handle->write(std::unique_ptr<char[], decltype(deleter)>(reinterpret_cast<char*>(ptr), deleter), size);
		}


		virtual void stop() {
			if(_is_connected) {
				handle->shutdown();
				_is_connected = false;
			}
			if(_is_open) {
				handle->stop();
				handle->close();
				_is_open = false;
			}
		}
	};
}


#endif
