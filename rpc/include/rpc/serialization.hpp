#ifndef RPC_SERIALIZATION_HPP
#define RPC_SERIALIZATION_HPP


#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <variant>
#include <vector>

#include <tcb/span.hpp>


#define MAP_OUT
#define EVAL0(...) __VA_ARGS__
#define EVAL1(...) EVAL0(EVAL0(EVAL0(__VA_ARGS__)))
#define EVAL2(...) EVAL1(EVAL1(EVAL1(__VA_ARGS__)))
// #define EVAL3(...) EVAL2(EVAL2(EVAL2(__VA_ARGS__)))
// #define EVAL4(...) EVAL3(EVAL3(EVAL3(__VA_ARGS__)))
#define EVAL(...) EVAL2(EVAL2(EVAL2(__VA_ARGS__)))
#define MAP_END(...)
#define MAP_GET_END() 0, MAP_END
#define MAP_NEXT0(item, next, ...) next MAP_OUT
#define MAP_NEXT1(item, next) MAP_NEXT0(item, next, 0)
#define MAP_NEXT(item, next) MAP_NEXT1(MAP_GET_END item, next)
#define MAP0(f, data, x, peek, ...) f(data, x) MAP_NEXT(peek, MAP1) (f, data, peek, __VA_ARGS__)
#define MAP1(f, data, x, peek, ...) f(data, x) MAP_NEXT(peek, MAP0) (f, data, peek, __VA_ARGS__)
#define MAP(f, data, ...) EVAL(MAP1(f, data, __VA_ARGS__, (), 0))



#define RPC_SERIALIZE_FIELD(_, field_name) serialize_to(data.field_name, to);
#define RPC_DESERIALIZE_FIELD(_, field_name) deserialize_to(ptr, end, to.field_name);

#define RPC_DEFINE_SERIALIZE(struct_name, ...) \
	inline void serialize_to(const struct_name& data, std::vector<std::byte>& to) { \
		using rpc::serialize_to; \
		MAP(RPC_SERIALIZE_FIELD, _, __VA_ARGS__) \
	} \
	inline void deserialize_to(const std::byte*& ptr, const std::byte* end, struct_name& to) { \
		using rpc::deserialize_to; \
		MAP(RPC_DESERIALIZE_FIELD, _, __VA_ARGS__) \
	}


namespace rpc {
	template<typename T, typename = std::enable_if_t<std::is_integral_v<T>>> void serialize_to(T num, std::vector<std::byte>& to) {
		std::byte* begin = reinterpret_cast<std::byte*>(&num);
		std::byte* end = begin + sizeof(num);
		if constexpr(std::endian::native == std::endian::little) {
			std::reverse(begin, end);
		}
		to.insert(to.end(), begin, end);
	}

	inline void serialize_to(std::byte data, std::vector<std::byte>& to) {
		to.push_back(data);
	}

	template<typename T> void serialize_to(const std::vector<T>& data, std::vector<std::byte>& to);
	template<typename... Types> void serialize_to(const std::variant<Types...>& data, std::vector<std::byte>& to);
	template<typename... Types> void serialize_to(const std::tuple<Types...>& data, std::vector<std::byte>& to);
	template<typename First, typename Second> void serialize_to(const std::pair<First, Second>& data, std::vector<std::byte>& to);
	template<typename T, size_t N> void serialize_to(const std::array<T, N>& data, std::vector<std::byte>& to);
	void serialize_to(const std::string& data, std::vector<std::byte>& to);

	template<typename T> void serialize_to(const std::vector<T>& data, std::vector<std::byte>& to) {
		serialize_to(static_cast<uint64_t>(data.size()), to);
		for(const auto& elem: data) {
			serialize_to(elem, to);
		}
	}

	template<typename... Types> void serialize_to(const std::variant<Types...>& data, std::vector<std::byte>& to) {
		serialize_to(static_cast<uint8_t>(data.index()), to);
		std::visit([&to](const auto& value) {
			serialize_to(value, to);
		}, data);
	}

	template<typename... Types, size_t... I> void _serialize_tuple_helper(const std::tuple<Types...>& data, std::vector<std::byte>& to, std::index_sequence<I...>) {
		(serialize_to(std::get<I>(data), to), ...);
	}

	template<typename... Types> void serialize_to(const std::tuple<Types...>& data, std::vector<std::byte>& to) {
		_serialize_tuple_helper(data, to, std::make_index_sequence<sizeof...(Types)>());
	}

	template<typename First, typename Second> void serialize_to(const std::pair<First, Second>& data, std::vector<std::byte>& to) {
		serialize_to(data.first, to);
		serialize_to(data.second, to);
	}

	template<typename T, size_t N> void serialize_to(const std::array<T, N>& data, std::vector<std::byte>& to) {
		for(const auto& elem: data) {
			serialize_to(elem, to);
		}
	}

	inline void serialize_to(const std::string& data, std::vector<std::byte>& to) {
		serialize_to(static_cast<uint64_t>(data.size()), to);
		size_t old_to_size = to.size();
		to.resize(old_to_size + data.size());
		std::memcpy(to.data() + old_to_size, data.data(), data.size());
	}

	template<typename T> std::vector<std::byte> serialize(const T& data) {
		std::vector<std::byte> to;
		serialize_to(data, to);
		return to;
	}



	template<typename T, typename = std::enable_if_t<std::is_integral_v<T>>> void deserialize_to(const std::byte*& ptr, const std::byte* end, T& to) {
		if(static_cast<size_t>(end - ptr) < sizeof(T)) {
			throw std::invalid_argument("Invalid serialized value (integral type)");
		}
		std::byte* num_begin = reinterpret_cast<std::byte*>(&to);
		std::byte* num_end = num_begin + sizeof(T);
		std::copy(ptr, ptr + sizeof(T), num_begin);
		ptr += sizeof(T);
		if constexpr(std::endian::native == std::endian::little) {
			std::reverse(num_begin, num_end);
		}
	}

	inline void deserialize_to(const std::byte*& ptr, const std::byte* end, std::byte& to) {
		if(ptr == end) {
			throw std::invalid_argument("Invalid serialized value (std::byte)");
		}
		to = *ptr++;
	}

	template<typename T> void deserialize_to(const std::byte*& ptr, const std::byte* end, std::vector<T>& to);
	template<typename... Types> void deserialize_to(const std::byte*& ptr, const std::byte* end, std::variant<Types...>& to);
	template<typename... Types> void deserialize_to(const std::byte*& ptr, const std::byte* end, std::tuple<Types...>& to);
	template<typename First, typename Second> void deserialize_to(const std::byte*& ptr, const std::byte* end, std::pair<First, Second>& to);
	template<typename T, size_t N> void deserialize_to(const std::byte*& ptr, const std::byte* end, std::array<T, N>& to);
	void deserialize_to(const std::byte*& ptr, const std::byte* end, std::string& to);

	template<typename T> void deserialize_to(const std::byte*& ptr, const std::byte* end, std::vector<T>& to) {
		uint64_t size;
		deserialize_to(ptr, end, size);
		to.resize(size);
		for(uint64_t i = 0; i < size; i++) {
			deserialize_to(ptr, end, to[i]);
		}
	}

	template<typename... Types, size_t... I> void _deserialize_to_variant_helper(const std::byte*& ptr, const std::byte* end, std::variant<Types...>& to, std::index_sequence<I...>) {
		uint8_t index;
		deserialize_to(ptr, end, index);
		if(index >= sizeof...(Types)) {
			throw std::invalid_argument("Invalid serialized value (std::variant)");
		}
		static constexpr std::array fns{
			+[](const std::byte*& ptr, const std::byte* end, std::variant<Types...>& to) {
				deserialize_to(ptr, end, std::get<I>(to));
			}...
		};
		fns[index](ptr, end, to);
	}

	template<typename... Types> void deserialize_to(const std::byte*& ptr, const std::byte* end, std::variant<Types...>& to) {
		_deserialize_to_variant_helper(ptr, end, to, std::make_index_sequence<sizeof...(Types)>());
	}

	template<typename... Types, size_t... I> void _deserialize_to_tuple_helper(const std::byte*& ptr, const std::byte* end, std::tuple<Types...>& to, std::index_sequence<I...>) {
		(deserialize_to(ptr, end, std::get<I>(to)), ...);
	}

	template<typename... Types> void deserialize_to(const std::byte*& ptr, const std::byte* end, std::tuple<Types...>& to) {
		_deserialize_to_tuple_helper(ptr, end, to, std::make_index_sequence<sizeof...(Types)>());
	}

	template<typename First, typename Second> void deserialize_to(const std::byte*& ptr, const std::byte* end, std::pair<First, Second>& to) {
		deserialize_to(ptr, end, to.first);
		deserialize_to(ptr, end, to.second);
	}

	template<typename T, size_t N> void deserialize_to(const std::byte*& ptr, const std::byte* end, std::array<T, N>& to) {
		for(size_t i = 0; i < N; i++) {
			deserialize_to(ptr, end, to[i]);
		}
	}

	inline void deserialize_to(const std::byte*& ptr, const std::byte* end, std::string& to) {
		uint64_t size;
		deserialize_to(ptr, end, size);
		if(size > static_cast<size_t>(end - ptr)) {
			throw std::invalid_argument("Invalid serialized value (std::string)");
		}
		to.resize(size);
		std::memcpy(to.data(), ptr, size);
		ptr += size;
	}

	template<typename T> T deserialize(tcb::span<const std::byte> data) {
		T to;
		const std::byte* ptr = data.begin();
		const std::byte* end = data.end();
		deserialize_to(ptr, end, to);
		if(ptr != end) {
			throw std::invalid_argument("Trailing junk in serialized data");
		}
		return to;
	}

	template<> inline void deserialize<void>(tcb::span<const std::byte> data) {
		if(!data.empty()) {
			throw std::invalid_argument("Trailing junk in serialized data");
		}
	}

	template<typename T> T deserialize(const std::vector<std::byte>& data) {
		return deserialize<T>(tcb::span<const std::byte>(data.data(), data.size()));
	}

	template<typename T> T deserialize(const std::string& data) {
		return deserialize<T>(tcb::span<const std::byte>(reinterpret_cast<const std::byte*>(data.data()), data.size()));
	}



	inline std::string join_strings(const std::string& joiner, const std::vector<std::string>& strings) {
		std::string result;
		if(!strings.empty()) {
			result = strings[0];
			for(size_t i = 1; i < strings.size(); i++) {
				result += joiner;
				result += strings[i];
			}
		}
		return result;
	}

	template<typename T, typename Enable = void> struct type_string {
	};
	template<typename T> struct type_string<T, std::enable_if_t<std::is_integral_v<T>>> {
		static inline std::string text = (std::is_unsigned_v<T> ? "uint" : "int") + std::to_string(sizeof(T) * CHAR_BIT) + "_t";
	};
	template<> struct type_string<void> {
		static inline std::string text = "void";
	};
	template<> struct type_string<std::byte> {
		static inline std::string text = "byte";
	};
	template<> struct type_string<std::string> {
		static inline std::string text = "string";
	};
	template<typename... Types> struct type_string<std::variant<Types...>> {
		static inline std::string text = "variant<" + join_strings(", ", {type_string<Types>::text...}) + ">";
	};
	template<typename... Types> struct type_string<std::tuple<Types...>> {
		static inline std::string text = "tuple<" + join_strings(", ", {type_string<Types>::text...}) + ">";
	};
	template<typename First, typename Second> struct type_string<std::pair<First, Second>> {
		static inline std::string text = "pair<" + type_string<First>::text + ", " + type_string<Second>::text + ">";
	};
	template<typename T, size_t N> struct type_string<std::array<T, N>> {
		static inline std::string text = "array<" + type_string<T>::text + ", " + std::to_string(N) + ">";
	};
	template<typename R, typename... Args> struct type_string<R(Args...)> {
		static inline std::string text = type_string<R>::text + "(" + join_strings(", ", {type_string<Args>::text...}) + + ")";
	};
	template<typename T> std::string stringify_type() {
		return type_string<T>::text;
	}
}


#endif
