#pragma once

#include <string>
#include <vector>
#include <map>

// Forward declare protobuf types
namespace transferqueue {
class Trajectory;
class TrajectoryGroup;
class ChatMessage;
}

namespace transfer_queue {

/// 单条对话消息的内存表示
struct ChatMessageData {
    std::string role;     // "system" | "user" | "assistant" | "tool"
    std::string content;
};

/// 单条轨迹的内存表示
struct TrajectoryData {
    std::string uid;           // 全局唯一轨迹 ID
    std::string instance_id;   // 问题实例 ID
    std::vector<ChatMessageData> messages;
    double reward = 0.0;
    std::map<std::string, std::string> extra_info;
};

/// 轨迹组的内存表示
struct TrajectoryGroupData {
    std::string instance_id;
    std::vector<TrajectoryData> trajectories;
    int32_t group_size = 0;    // 目标组大小
    bool is_complete = false;  // 是否已满（ready）
};

// ============================================================================
// Protobuf ↔ 内存结构转换
// ============================================================================

/// 从 Protobuf ChatMessage 转换为 ChatMessageData
ChatMessageData from_proto(const transferqueue::ChatMessage& proto);

/// 将 ChatMessageData 转换为 Protobuf ChatMessage
void to_proto(const ChatMessageData& data, transferqueue::ChatMessage* proto);

/// 从 Protobuf Trajectory 转换为 TrajectoryData
TrajectoryData from_proto(const transferqueue::Trajectory& proto);

/// 将 TrajectoryData 转换为 Protobuf Trajectory
void to_proto(const TrajectoryData& data, transferqueue::Trajectory* proto);

/// 从 Protobuf TrajectoryGroup 转换为 TrajectoryGroupData
TrajectoryGroupData from_proto(const transferqueue::TrajectoryGroup& proto);

/// 将 TrajectoryGroupData 转换为 Protobuf TrajectoryGroup
void to_proto(const TrajectoryGroupData& data, transferqueue::TrajectoryGroup* proto);

} // namespace transfer_queue
