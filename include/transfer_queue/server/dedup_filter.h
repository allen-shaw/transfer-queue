#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

namespace transfer_queue {

/// UID 去重过滤器
///
/// 使用 bloom filter 做快速判定 + LRU 精确集合做确认。
/// 每个 BufferShard 持有一个独立的 DedupFilter（无锁）。
class DedupFilter {
public:
    /// 构造函数
    /// @param expected_size bloom filter 预期元素数量
    /// @param false_positive_rate 可接受的误判率
    explicit DedupFilter(size_t expected_size = 100000, double false_positive_rate = 0.01);

    ~DedupFilter();

    /// 检查 uid 是否已存在（可能有误判）
    /// @param uid 要检查的唯一标识
    /// @return true 表示已存在（应丢弃），false 表示不存在
    bool contains(const std::string& uid) const;

    /// 插入 uid
    /// @param uid 要插入的唯一标识
    /// @return true 表示新插入成功，false 表示已存在（去重）
    bool insert(const std::string& uid);

    /// 清空所有记录
    void clear();

    /// 当前已记录的 uid 数量
    size_t size() const;

private:
    // TODO: bloom filter 实现
    // TODO: LRU 精确集合
    std::unordered_set<std::string> exact_set_;  // 临时用精确集合，后续加 bloom filter
};

} // namespace transfer_queue
