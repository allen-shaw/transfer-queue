#pragma once

#include <seastar/rpc/rpc.hh>
#include <seastar/core/sstring.hh>

/// Custom Seastar RPC serializer that supports basic types and sstring.
/// Protobuf messages are transported as sstring (serialized bytes).
struct serializer {};

// --- Arithmetic types ---

template <typename T, typename Output>
inline void write_arithmetic_type(Output& out, T v) {
    static_assert(std::is_arithmetic_v<T>, "must be arithmetic type");
    return out.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <typename T, typename Input>
inline T read_arithmetic_type(Input& in) {
    static_assert(std::is_arithmetic_v<T>, "must be arithmetic type");
    T v;
    in.read(reinterpret_cast<char*>(&v), sizeof(T));
    return v;
}

template <typename Output>
inline void write(serializer, Output& output, int32_t v) { return write_arithmetic_type(output, v); }
template <typename Output>
inline void write(serializer, Output& output, uint32_t v) { return write_arithmetic_type(output, v); }
template <typename Output>
inline void write(serializer, Output& output, int64_t v) { return write_arithmetic_type(output, v); }
template <typename Output>
inline void write(serializer, Output& output, uint64_t v) { return write_arithmetic_type(output, v); }

template <typename Input>
inline int32_t read(serializer, Input& input, seastar::rpc::type<int32_t>) { return read_arithmetic_type<int32_t>(input); }
template <typename Input>
inline uint32_t read(serializer, Input& input, seastar::rpc::type<uint32_t>) { return read_arithmetic_type<uint32_t>(input); }
template <typename Input>
inline int64_t read(serializer, Input& input, seastar::rpc::type<int64_t>) { return read_arithmetic_type<int64_t>(input); }
template <typename Input>
inline uint64_t read(serializer, Input& input, seastar::rpc::type<uint64_t>) { return read_arithmetic_type<uint64_t>(input); }

// --- sstring (used to carry serialized protobuf bytes) ---

template <typename Output>
inline void write(serializer, Output& out, const seastar::sstring& v) {
    write_arithmetic_type(out, uint32_t(v.size()));
    out.write(v.c_str(), v.size());
}

template <typename Input>
inline seastar::sstring read(serializer, Input& in, seastar::rpc::type<seastar::sstring>) {
    auto size = read_arithmetic_type<uint32_t>(in);
    seastar::sstring ret = seastar::uninitialized_string(size);
    in.read(ret.data(), size);
    return ret;
}
