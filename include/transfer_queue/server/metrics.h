#pragma once

#include <seastar/core/future.hh>
#include <seastar/core/metrics_registration.hh>


#include "transfer_queue/server/buffer_manager.h"

namespace transfer_queue {

/// 监控指标采集器
///
/// 注册 Seastar metrics，暴露 Buffer 状态信息，
/// 可被 Prometheus 等监控系统抓取。
class Metrics {
public:
    /// 构造函数
    /// @param manager Buffer 管理器引用（用于查询状态）
    explicit Metrics(BufferManager& manager);

    ~Metrics();

    /// 注册所有指标
    seastar::future<> start();

    /// 注销指标
    seastar::future<> stop();

private:
    /// 注册 Seastar metrics group
    void register_metrics();

    BufferManager& manager_;
    seastar::metrics::metric_groups metric_groups_;
};


} // namespace transfer_queue
