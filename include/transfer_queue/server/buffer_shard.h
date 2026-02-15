#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

#include <seastar/core/future.hh>
#include <seastar/core/sstring.hh>

#include "transfer_queue/common/config.h"
#include "transfer_queue/server/dedup_filter.h"
#include "transferqueue.pb.h"

namespace transfer_queue {

/// 单个 core 上的分片存储
///
/// 按 hash(instance_id) % smp::count 路由，同一 instance 的数据
/// 总在同一个 BufferShard 上处理，实现无锁聚合。
///
/// 职责：
/// - 按 instance_id 管理轨迹组
/// - Group 聚合：轨迹数达到 group_size 后标记 ready
/// - 消费式读取：读取后删除
/// - UID 去重
class BufferShard {
public:
    /// 构造函数
    /// @param config 全局配置（引用，可动态更新）
    explicit BufferShard(const TransferQueueConfig& config);

    ~BufferShard();

    // ========================================================================
    // 写入
    // ========================================================================

    /// 写入单条轨迹
    /// @param trajectory 要写入的轨迹数据
    /// @return future<bool>: true 表示写入成功，false 表示 uid 重复被去重
    seastar::future<bool> write(transferqueue::Trajectory trajectory);

    /// 批量写入轨迹
    /// @param trajectories 要写入的轨迹列表
    /// @return future<int32_t>: 实际写入的条数（去重后）
    seastar::future<int32_t> batch_write(std::vector<transferqueue::Trajectory> trajectories);

    // ========================================================================
    // 读取
    // ========================================================================

    /// 读取所有已就绪（ready）的轨迹组，读后删除
    /// @param max_groups 最多返回多少组，0 表示不限
    /// @return 已就绪的轨迹组列表（unique_ptr 包装以兼容 Seastar future）
    seastar::future<std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>>
    read_ready_groups(int32_t max_groups = 0);

    /// 当前是否有已就绪的组
    seastar::future<bool> has_ready_groups() const;

    // ========================================================================
    // 管理
    // ========================================================================

    /// 删除指定 instance_id 的所有数据
    seastar::future<> delete_instance(const std::string& instance_id);

    /// 清空本 shard 的所有数据
    seastar::future<> reset();

    /// 获取本 shard 的状态
    seastar::future<std::unique_ptr<transferqueue::BufferStatus>> get_status() const;

    /// 获取本 shard 的元信息（用于聚合到全局 MetaInfo）
    seastar::future<std::unique_ptr<transferqueue::MetaInfo>> get_meta_info() const;

    // ========================================================================
    // 配置
    // ========================================================================

    /// 动态更新配置
    void update_config(const TransferQueueConfig& config);

private:
    /// 检查超时的未满组，标记为 ready
    seastar::future<> check_group_timeouts();

    /// 计算当前内存使用量
    size_t estimate_memory_usage() const;

    const TransferQueueConfig* config_;
    DedupFilter dedup_filter_;

    // instance_id -> TrajectoryGroup
    std::unordered_map<std::string, transferqueue::TrajectoryGroup> groups_;

    // 统计
    int64_t total_trajectories_ = 0;
    int64_t total_consumed_ = 0;
};

} // namespace transfer_queue
