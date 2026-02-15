#pragma once

#include <string>
#include <vector>
#include <memory>

#include <seastar/core/future.hh>
#include <seastar/core/file.hh>
#include <seastar/core/iostream.hh>
#include <optional>

#include "transferqueue.pb.h"

namespace transfer_queue {

/// 异步磁盘存储引擎
///
/// 负责将内存中的数据溢出到磁盘，以及从磁盘加载回内存。
/// 使用 Seastar 原生文件 API (DMA) 实现高性能异步 I/O。
///
/// 触发条件：内存使用率 >= spill_to_disk_threshold (默认 80%)
class DiskStorage {
public:
    /// 构造函数
    /// @param bdev_name 存储目录路径
    explicit DiskStorage(const std::string& bdev_name);

    ~DiskStorage();

    /// 初始化存储目录
    seastar::future<> init();

    /// 关闭存储，释放资源
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
    seastar::file file_;
    uint64_t write_offset_ = 0;
    std::optional<seastar::output_stream<char>> out_;
};

} // namespace transfer_queue
