#include "transfer_queue/client/client.h"

namespace transfer_queue {

TransferQueueClient::TransferQueueClient(const std::string& host, uint16_t port)
    : host_(host), port_(port), streaming_task_(seastar::make_ready_future<>()) {
}

TransferQueueClient::~TransferQueueClient() {
    // Stop streaming if active
    if (streaming_active_) {
        streaming_active_ = false;
        auto fut = streaming_task_.handle_exception([](auto ep) {
            // Ignore exceptions during cleanup
        });
        // We can't wait in destructor, so we just abandon the future
        (void)fut;
    }
}

// ============================================================================
// 连接管理
// ============================================================================

seastar::future<> TransferQueueClient::connect() {
    if (connected_) {
        return seastar::make_ready_future<>();
    }
    
    try {
        // Create RPC protocol with serializer
        transfer_queue::RpcHandler::PbSerializer serializer;
        rpc_proto_ = std::make_unique<seastar::rpc::protocol<transfer_queue::RpcHandler::PbSerializer>>(std::move(serializer));
        
        // Create RPC client
        auto addr = seastar::make_ipv4_address({host_, port_});
        rpc_client_ = std::make_unique<seastar::rpc::protocol<transfer_queue::RpcHandler::PbSerializer>::client>(
            *rpc_proto_, addr);
        
        connected_ = true;
        return seastar::make_ready_future<>();
    } catch (const std::exception& e) {
        return seastar::make_exception_future<>(
            std::runtime_error(std::string("Failed to connect: ") + e.what()));
    }
}

seastar::future<> TransferQueueClient::close() {
    if (!connected_) {
        return seastar::make_ready_future<>();
    }
    
    try {
        // Close RPC client
        rpc_client_.reset();
        rpc_proto_.reset();
        connected_ = false;
        return seastar::make_ready_future<>();
    } catch (const std::exception& e) {
        return seastar::make_exception_future<>(
            std::runtime_error(std::string("Failed to close: ") + e.what()));
    }
}

bool TransferQueueClient::is_connected() const {
    return connected_;
}

// ============================================================================
// 写入
// ============================================================================

seastar::future<int32_t> TransferQueueClient::batch_write(
        std::vector<transferqueue::Trajectory> trajectories) {
    if (!connected_) {
        return seastar::make_exception_future<int32_t>(
            std::runtime_error("Client not connected"));
    }
    
    // Build request
    transferqueue::BatchWriteRequest req;
    for (auto& traj : trajectories) {
        *req.add_trajectories() = std::move(traj);
    }
    
    // Serialize request
    std::string serialized;
    if (!req.SerializeToString(&serialized)) {
        return seastar::make_exception_future<int32_t>(
            std::runtime_error("Failed to serialize request"));
    }
    
    // Create client callable for BATCH_WRITE verb
    auto write_client = rpc_proto_->make_client<seastar::sstring(seastar::sstring)>(
        static_cast<uint32_t>(RpcHandler::Verb::BATCH_WRITE));
    
    return write_client(*rpc_client_, seastar::sstring(serialized))
        .then([](seastar::sstring response_data) {
            transferqueue::BatchWriteResponse resp;
            if (!resp.ParseFromString(response_data)) {
                throw std::runtime_error("Failed to parse response");
            }
            if (!resp.success()) {
                throw std::runtime_error("Batch write failed");
            }
            return resp.written_count();
        });
}

seastar::future<bool> TransferQueueClient::write(transferqueue::Trajectory trajectory) {
    std::vector<transferqueue::Trajectory> trajectories;
    trajectories.push_back(std::move(trajectory));
    return batch_write(std::move(trajectories)).then([](int32_t count) {
        return count > 0;
    });
}

// ============================================================================
// 读取
// ============================================================================

seastar::future<std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>>
TransferQueueClient::batch_read(int32_t max_groups, bool block, int32_t timeout_ms) {
    if (!connected_) {
        return seastar::make_exception_future<std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>>(
            std::runtime_error("Client not connected"));
    }
    
    // Build request
    transferqueue::BatchReadRequest req;
    req.set_max_groups(max_groups);
    req.set_block(block);
    req.set_timeout_ms(timeout_ms);
    
    // Serialize request
    std::string serialized;
    if (!req.SerializeToString(&serialized)) {
        return seastar::make_exception_future<std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>>(
            std::runtime_error("Failed to serialize request"));
    }
    
    // Create client callable for BATCH_READ verb
    auto read_client = rpc_proto_->make_client<seastar::sstring(seastar::sstring)>(
        static_cast<uint32_t>(RpcHandler::Verb::BATCH_READ));
    
    return read_client(*rpc_client_, seastar::sstring(serialized))
        .then([](seastar::sstring response_data) {
            transferqueue::BatchReadResult result;
            if (!result.ParseFromString(response_data)) {
                throw std::runtime_error("Failed to parse response");
            }
            if (!result.success()) {
                throw std::runtime_error("Batch read failed: " + result.message());
            }
            
            std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>> groups;
            for (auto& group : *result.mutable_groups()) {
                auto group_ptr = std::make_unique<transferqueue::TrajectoryGroup>();
                group_ptr->Swap(&group);
                groups.push_back(std::move(group_ptr));
            }
            return groups;
        });
}

// ============================================================================
// 状态查询
// ============================================================================

seastar::future<std::unique_ptr<transferqueue::BufferStatus>>
TransferQueueClient::get_status() {
    if (!connected_) {
        return seastar::make_exception_future<std::unique_ptr<transferqueue::BufferStatus>>(
            std::runtime_error("Client not connected"));
    }
    
    // Build empty request
    transferqueue::GetStatusRequest req;
    std::string serialized;
    if (!req.SerializeToString(&serialized)) {
        return seastar::make_exception_future<std::unique_ptr<transferqueue::BufferStatus>>(
            std::runtime_error("Failed to serialize request"));
    }
    
    // Create client callable for GET_STATUS verb
    auto status_client = rpc_proto_->make_client<seastar::sstring(seastar::sstring)>(
        static_cast<uint32_t>(RpcHandler::Verb::GET_STATUS));
    
    return status_client(*rpc_client_, seastar::sstring(serialized))
        .then([](seastar::sstring response_data) {
            auto status = std::make_unique<transferqueue::BufferStatus>();
            if (!status->ParseFromString(response_data)) {
                throw std::runtime_error("Failed to parse response");
            }
            return status;
        });
}

// ============================================================================
// 流式订阅
// ============================================================================

seastar::future<> TransferQueueClient::subscribe(
        int32_t prefetch_groups,
        std::function<seastar::future<>(const transferqueue::TrajectoryGroup&)> callback) {
    if (!connected_) {
        return seastar::make_exception_future<>(
            std::runtime_error("Client not connected"));
    }
    
    if (streaming_active_) {
        return seastar::make_exception_future<>(
            std::runtime_error("Already subscribed"));
    }
    
    // Build request
    transferqueue::SubscribeRequest req;
    req.set_prefetch_groups(prefetch_groups);
    std::string serialized;
    if (!req.SerializeToString(&serialized)) {
        return seastar::make_exception_future<>(
            std::runtime_error("Failed to serialize request"));
    }
    
    // Create streaming RPC call - server expects (sstring, sink<sstring>) -> future<>
    auto subscribe_client = rpc_proto_->make_client<seastar::rpc::source<seastar::sstring>(seastar::sstring, seastar::rpc::sink<seastar::sstring>)>(
        static_cast<uint32_t>(RpcHandler::Verb::SUBSCRIBE));
    
    return rpc_client_->make_stream_sink<RpcHandler::PbSerializer, seastar::sstring>().then(
        [this, subscribe_client, serialized, callback](seastar::rpc::sink<seastar::sstring> sink) mutable {
            // Call subscribe RPC - server will return a source for streaming back to us
            return subscribe_client(*rpc_client_, seastar::sstring(serialized), std::move(sink))
                .then([this, callback](seastar::rpc::source<seastar::sstring> source) {
                    // Set up streaming state
                    streaming_active_ = true;
                    
                    // Start background task to receive messages
                    streaming_task_ = seastar::do_until(
                        [this] { return !streaming_active_; },
                        [this, callback, source]() mutable {
                            return source().then([callback](std::optional<std::tuple<seastar::sstring>> data) {
                                if (!data) {
                                    // End of stream - server closed connection
                                    throw std::runtime_error("Stream ended");
                                }
                                
                                // Parse TrajectoryGroup from received data
                                transferqueue::TrajectoryGroup group;
                                if (!group.ParseFromString(std::get<0>(*data))) {
                                    throw std::runtime_error("Failed to parse trajectory group");
                                }
                                
                                // Call user callback
                                return callback(group);
                            });
                        }
                    ).handle_exception([this](auto ep) {
                        // Handle streaming errors and set inactive state
                        streaming_active_ = false;
                        try {
                            std::rethrow_exception(ep);
                        } catch (const std::exception& e) {
                            // Log error or handle as needed
                        }
                    });
                    
                    return seastar::make_ready_future<>();
                });
        });
}

seastar::future<> TransferQueueClient::unsubscribe() {
    if (!connected_) {
        return seastar::make_exception_future<>(
            std::runtime_error("Client not connected"));
    }
    
    if (!streaming_active_) {
        return seastar::make_ready_future<>();
    }
    
    // Stop streaming by canceling the background task
    streaming_active_ = false;
    
    // Cancel the streaming task and wait for it to complete
    return streaming_task_.handle_exception([](auto ep) {
        // Ignore exceptions during cancellation
    }).then([this] {
        return seastar::make_ready_future<>();
    });
}

} // namespace transfer_queue
