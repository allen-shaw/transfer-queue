#pragma once

// 直接使用 Protobuf 生成的类作为数据模型，无需额外定义结构体。
//
// 核心类型：
//   transferqueue::ChatMessage      — 单条对话消息
//   transferqueue::Trajectory       — 单条轨迹
//   transferqueue::TrajectoryGroup  — 轨迹组（按 instance_id 聚合）
//   transferqueue::MetaInfo         — 元信息
//   transferqueue::BufferStatus     — Buffer 状态
//
// 注意：Protobuf 生成的类不满足 nothrow_move_constructible，
// 不能直接作为 seastar::future<T> 的值类型。
// 需要用 std::unique_ptr<T> 包装，或者使用 seastar::future<foreign_ptr<...>>。

#include "transferqueue.pb.h" // IWYU pragma: export
