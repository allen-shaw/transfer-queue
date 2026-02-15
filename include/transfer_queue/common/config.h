#pragma once

#include <cstdint>
#include <string>

namespace transfer_queue {

/// TransferQueue 全局配置
struct TransferQueueConfig {
    // --- 网络 ---
    uint16_t http_port = 8889;          // HTTP REST API 端口
    uint16_t rpc_port = 8890;           // Seastar RPC 端口

    // --- Group 聚合 ---
    int32_t group_size = 16;            // 每组轨迹数 (n_samples_per_prompt)
    int32_t group_timeout_seconds = 300; // 组超时秒数（超时后即使未满也标记 ready）

    // --- 去重 ---
    bool uid_dedup = true;              // 是否启用 UID 去重

    // --- 内存管理 ---
    int64_t max_memory_bytes = 8LL * 1024 * 1024 * 1024;  // 最大内存 (默认 8GB)
    double spill_to_disk_threshold = 0.8;                   // SPDK 溢出阈值

    // --- SPDK ---
    bool spdk_enabled = false;          // 是否启用 SPDK 存储
    std::string spdk_bdev_name;         // SPDK 块设备名

    // --- 任务类型 ---
    std::string task_type = "math";     // 任务类型标识
};

/// 从命令行参数解析配置
TransferQueueConfig parse_config(int argc, char** argv);

/// 动态更新配置（运行时 /config API）
void update_config(TransferQueueConfig& config, const TransferQueueConfig& update);

} // namespace transfer_queue
