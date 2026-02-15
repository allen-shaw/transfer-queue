#pragma once

#include <string>
#include <vector>
#include <memory>

#include <seastar/core/future.hh>

#include "transferqueue.pb.h"

namespace transfer_queue {

/// SPDK 异步存储引擎
///
/// 负责将内存中的数据溢出到 SPDK 块设备，以及从磁盘加载回内存。
/// 在专用 core 上运行，不阻塞写入路径。
///
/// 触发条件：内存使用率 >= spill_to_disk_threshold (默认 80%)
class SpdkStorage {
public:
    /// 构造函数
    /// @param bdev_name SPDK 块设备名称
    explicit SpdkStorage(const std::string& bdev_name);

    ~SpdkStorage();

    /// 初始化 SPDK 环境
    seastar::future<> init();

    /// 关闭 SPDK，释放资源
    seastar::future<> shutdown();

    // ========================================================================
    // 写入（溢出到磁盘）
    // ========================================================================

    /// 将轨迹组数据异步写入磁盘
    /// @param group 要持久化的轨迹组
    /// @return 写入成功后的磁盘偏移/key，用于后续加载
    seastar::future<uint64_t> spill(const transferqueue::TrajectoryGroup& group);

    /// 批量溢出多个轨迹组
    seastar::future<std::vector<uint64_t>> spill_batch(
        const std::vector<transferqueue::TrajectoryGroup>& groups);

    // ========================================================================
    // 读取（从磁盘加载）
    // ========================================================================

    /// 从磁盘加载轨迹组数据
    /// @param offset spill 时返回的偏移/key
    seastar::future<std::unique_ptr<transferqueue::TrajectoryGroup>> load(uint64_t offset);

    /// 批量加载
    seastar::future<std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>>
    load_batch(const std::vector<uint64_t>& offsets);

    // ========================================================================
    // 管理
    // ========================================================================

    /// 删除磁盘上指定 offset 的数据
    seastar::future<> remove(uint64_t offset);

    /// 获取磁盘使用量（字节）
    seastar::future<int64_t> get_disk_usage() const;

    /// 是否已初始化
    bool is_initialized() const;

private:
    std::string bdev_name_;
    bool initialized_ = false;
    // TODO: SPDK blobstore / bdev 句柄
};

} // namespace transfer_queue
