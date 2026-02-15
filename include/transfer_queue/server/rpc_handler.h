#pragma once

#include <seastar/core/future.hh>
#include <seastar/core/byteorder.hh>
#include <seastar/core/simple-stream.hh>
#include <seastar/rpc/rpc.hh>
#include <vector>
#include <type_traits>
#include <google/protobuf/message.h>

#include "transfer_queue/server/buffer_manager.h"

namespace transfer_queue {

/// Seastar RPC API 处理器
///
/// 提供高性能 Protobuf RPC 接口：
/// - BatchWrite   — 批量写入轨迹
/// - BatchRead    — 批量读取已就绪组（支持阻塞模式）
/// - GetStatus    — 查询 Buffer 状态
/// - Subscribe    — 流式推送就绪数据
class RpcHandler {
public:
    struct PbSerializer {};
    
    enum class Verb : uint32_t {

        BATCH_WRITE,
        BATCH_READ,
        GET_STATUS,
        SUBSCRIBE
    };

public:
    /// 构造函数
    /// @param manager Buffer 管理器引用
    explicit RpcHandler(BufferManager& manager);

    ~RpcHandler();

    /// 启动 RPC server 并监听指定端口
    /// @param port RPC 监听端口
    seastar::future<> start(uint16_t port);

    /// 停止 RPC server
    seastar::future<> stop();

private:
    /// 注册所有 RPC verb
    void register_handlers();

    // ========================================================================
    // RPC 处理函数
    // ========================================================================

    /// BatchWrite — 批量写入轨迹
    /// 接收 Protobuf 编码的 BatchWriteRequest，返回 BatchWriteResponse
    seastar::future<seastar::sstring> handle_batch_write(seastar::sstring request_data);

    /// BatchRead — 批量读取已就绪的轨迹组
    /// 接收 BatchReadRequest，支持阻塞等待模式，返回 BatchReadResult
    seastar::future<seastar::sstring> handle_batch_read(seastar::sstring request_data);

    /// GetStatus — 查询 Buffer 状态
    /// 返回 BufferStatus
    seastar::future<seastar::sstring> handle_get_status(seastar::sstring request_data);

    /// Subscribe — 流式推送
    /// 当有组就绪时主动推送 TrajectoryGroup 给客户端
    seastar::future<> handle_subscribe(seastar::sstring request_data,
                                       seastar::rpc::sink<seastar::sstring> sink);

    BufferManager& manager_;

    std::unique_ptr<seastar::rpc::protocol<PbSerializer>> rpc_proto_;
    std::unique_ptr<seastar::rpc::protocol<PbSerializer>::server> rpc_server_;


};

// Serialization Implementations
namespace internal {

// Stream helpers
template <typename Output, typename T>
void write_stream_le(Output& out, T v) {
    v = seastar::cpu_to_le(v);
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}

template <typename T, typename Input>
T read_stream_le(Input& in) {
    T v;
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    return seastar::le_to_cpu(v);
}



template <typename Output>
void write_le(Output& out, int32_t v) { write_stream_le(out, v); }
template <typename Output>
void write_le(Output& out, uint32_t v) { write_stream_le(out, v); }
template <typename Output>
void write_le(Output& out, int64_t v) { write_stream_le(out, v); }
template <typename Output>
void write_le(Output& out, uint64_t v) { write_stream_le(out, v); }
template <typename Output>
void write_le(Output& out, bool v) { write_stream_le(out, static_cast<uint8_t>(v)); }

template <typename Input>
int32_t read_le_int32(Input& in) { return read_stream_le<int32_t>(in); }
template <typename Input>
uint32_t read_le_uint32(Input& in) { return read_stream_le<uint32_t>(in); }
template <typename Input>
int64_t read_le_int64(Input& in) { return read_stream_le<int64_t>(in); }
template <typename Input>
uint64_t read_le_uint64(Input& in) { return read_stream_le<uint64_t>(in); }
template <typename Input>
bool read_le_bool(Input& in) { return read_stream_le<uint8_t>(in) != 0; }

// SString
template <typename Output>
void write_impl(Output& out, const seastar::sstring& v) {
    write_le(out, uint32_t(v.size()));
    out.write(v.data(), v.size());
}
template <typename Input>
seastar::sstring read_impl(Input& in, seastar::rpc::type<seastar::sstring>) {
    uint32_t size = read_le_uint32(in);
    seastar::sstring s = seastar::uninitialized_string(size);
    in.read(s.data(), size);
    return s;
}

// Protobuf
template <typename Output, typename T>
std::enable_if_t<std::is_base_of_v<google::protobuf::Message, T>, void>
write_impl(Output& out, const T& v) {
    std::string s;
    if (!v.SerializeToString(&s)) { throw std::runtime_error("Protobuf serialize failed"); }
    write_le(out, uint32_t(s.size()));
    out.write(s.data(), s.size());
}
template <typename Input, typename T>
std::enable_if_t<std::is_base_of_v<google::protobuf::Message, T>, T>
read_impl(Input& in, seastar::rpc::type<T>) {
    uint32_t size = read_le_uint32(in);
    std::string s(size, '\0');
    in.read(&s[0], size);
    T msg;
    if (!msg.ParseFromString(s)) { throw std::runtime_error("Protobuf parse failed"); }
    return msg;
}

// Vector
template <typename Output, typename T>
void write_impl(Output& out, const std::vector<T>& v);
template <typename Input, typename T>
std::vector<T> read_impl(Input& in, seastar::rpc::type<std::vector<T>>);

// Fundamental types dispatch
template <typename Output, typename T>
std::enable_if_t<std::is_arithmetic_v<T>, void>
write_impl(Output& out, const T& v) { return write_le(out, v); }

template <typename Input, typename T>
std::enable_if_t<std::is_same_v<T, int32_t>, T> read_impl(Input& in, seastar::rpc::type<T>) { return read_le_int32(in); }
template <typename Input, typename T>
std::enable_if_t<std::is_same_v<T, uint32_t>, T> read_impl(Input& in, seastar::rpc::type<T>) { return read_le_uint32(in); }
template <typename Input, typename T>
std::enable_if_t<std::is_same_v<T, int64_t>, T> read_impl(Input& in, seastar::rpc::type<T>) { return read_le_int64(in); }
template <typename Input, typename T>
std::enable_if_t<std::is_same_v<T, uint64_t>, T> read_impl(Input& in, seastar::rpc::type<T>) { return read_le_uint64(in); }
template <typename Input, typename T>
std::enable_if_t<std::is_same_v<T, bool>, T> read_impl(Input& in, seastar::rpc::type<T>) { return read_le_bool(in); }

} // namespace internal

// Forward declarations
template <typename Output, typename T>
inline void write(RpcHandler::PbSerializer& s, Output& out, const T& v);
template <typename Input, typename T>
inline T read(RpcHandler::PbSerializer& s, Input& in, seastar::rpc::type<T> t);

namespace internal {
// Vector implementation needs write/read visibility
template <typename Output, typename T>
void write_impl(Output& out, const std::vector<T>& v) {
    write_le(out, uint32_t(v.size()));
    RpcHandler::PbSerializer s;
    for (const auto& item : v) {
        write(s, out, item);
    }
}
template <typename Input, typename T>
std::vector<T> read_impl(Input& in, seastar::rpc::type<std::vector<T>>) {
    uint32_t size = read_le_uint32(in);
    std::vector<T> v;
    v.reserve(size);
    RpcHandler::PbSerializer s;
    for (uint32_t i = 0; i < size; ++i) {
        v.push_back(read(s, in, seastar::rpc::type<T>()));
    }
    return v;
}
} // namespace internal

template <typename Output, typename T>
inline void write(RpcHandler::PbSerializer&, Output& out, const T& v) {
    internal::write_impl(out, v);
}

template <typename Input, typename T>
inline T read(RpcHandler::PbSerializer&, Input& in, seastar::rpc::type<T> t) {
    return internal::read_impl(in, t);
}




} // namespace transfer_queue
