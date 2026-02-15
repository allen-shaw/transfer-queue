#pragma once

#include <memory>

#include <seastar/core/future.hh>
#include <seastar/core/sharded.hh>

#include "transfer_queue/common/config.h"
#include "transfer_queue/server/buffer_shard.h"
#include "transferqueue.pb.h"

namespace transfer_queue {

/// 全局 Buffer 管理器
///
/// 使用 Seastar sharded<BufferShard> 在所有 core 上分布存储。
/// 写入时按 hash(instance_id) % smp::count 路由到对应 shard。
/// 读取时跨 shard 聚合所有 ready group。
class BufferManager {
public:
    BufferManager(const TransferQueueConfig& config);
    ~BufferManager();

    /// 初始化：在所有 core 上构建 BufferShard
    seastar::future<> start();

    /// 停止：清理所有 shard
    seastar::future<> stop();

    // ========================================================================
    // 写入（自动路由到目标 shard）
    // ========================================================================

    /// 写入单条轨迹（按 instance_id 路由）
    seastar::future<bool> write(transferqueue::Trajectory trajectory);

    /// 批量写入轨迹（按 instance_id 分组后路由到各 shard）
    seastar::future<int32_t> batch_write(std::vector<transferqueue::Trajectory> trajectories);

    // ========================================================================
    // 读取（跨 shard 聚合）
    // ========================================================================

    /// 读取所有 shard 中已就绪的轨迹组
    /// @param max_groups 最多返回多少组，0 表示不限
    seastar::future<std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>>
    read_ready_groups(int32_t max_groups = 0);

    /// 阻塞式读取：等待数据就绪或超时
    /// @param max_groups 最多返回多少组
    /// @param timeout_ms 超时毫秒数
    seastar::future<std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>>
    read_ready_groups_blocking(int32_t max_groups, int32_t timeout_ms);

    /// 任意 shard 是否有 ready group
    seastar::future<bool> has_ready_groups() const;

    // ========================================================================
    // 管理
    // ========================================================================

    /// 删除指定 instance_id（路由到对应 shard）
    seastar::future<> delete_instance(const std::string& instance_id);

    /// 清空所有 shard
    seastar::future<> reset();

    /// 获取聚合后的 Buffer 状态
    seastar::future<std::unique_ptr<transferqueue::BufferStatus>> get_status() const;

    /// 动态更新配置（广播到所有 shard）
    seastar::future<> update_config(const transferqueue::ConfigRequest& config_req);

private:
    /// 计算 instance_id 对应的 shard id
    unsigned shard_for(const std::string& instance_id) const;

    TransferQueueConfig config_;
    seastar::sharded<BufferShard> shards_;
};

} // namespace transfer_queue
