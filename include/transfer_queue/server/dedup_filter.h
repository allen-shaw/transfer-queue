#pragma once

#include <cstddef>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace transfer_queue {

/// UID 去重过滤器
///
/// 两层结构：
///   1. Bloom filter — O(1) 快速排除。如果 bloom 说"不存在"，则一定不存在。
///   2. LRU 精确集合 — 确认 bloom 的命中是否为真正重复。
///
/// 当精确集合超过 max_size 时，淘汰最久未使用的 uid，并重建 bloom filter。
/// 每个 BufferShard 持有一个独立的 DedupFilter（无锁）。
class DedupFilter {
public:
    /// 构造函数
    /// @param max_size 精确集合最大容量，超过后 LRU 淘汰
    /// @param false_positive_rate bloom filter 目标误判率
    explicit DedupFilter(size_t max_size = 100000, double false_positive_rate = 0.01);

    ~DedupFilter();

    /// 检查 uid 是否已存在
    /// @return true 表示已存在（应丢弃）
    bool contains(const std::string& uid) const;

    /// 插入 uid
    /// @return true 表示新插入成功，false 表示已存在（去重）
    bool insert(const std::string& uid);

    /// 清空所有记录
    void clear();

    /// 当前已记录的 uid 数量
    size_t size() const;

    /// bloom filter 的误判率（测试用）
    double current_false_positive_rate() const;

    /// bloom filter 的 bit 数（测试用）
    size_t bloom_bit_count() const;

    /// bloom filter 的 hash 函数数（测试用）
    size_t bloom_hash_count() const;

private:
    // ========================================================================
    // Bloom filter
    // ========================================================================

    /// 计算 bloom filter 最优参数
    static size_t optimal_bit_count(size_t expected_elements, double fpr);
    static size_t optimal_hash_count(size_t bit_count, size_t expected_elements);

    /// 对 uid 计算第 i 个 hash 值
    size_t bloom_hash(const std::string& uid, size_t i) const;

    /// 在 bloom filter 中设置 uid 对应的位
    void bloom_add(const std::string& uid);

    /// 检查 uid 是否可能在 bloom filter 中
    bool bloom_maybe_contains(const std::string& uid) const;

    /// 用当前精确集合重建 bloom filter
    void bloom_rebuild();

    // ========================================================================
    // LRU 精确集合
    // ========================================================================

    /// 淘汰最旧的 uid，直到 size <= max_size_
    void evict_oldest();

    size_t max_size_;
    double false_positive_rate_;

    // Bloom filter 位向量
    std::vector<bool> bloom_bits_;
    size_t bloom_num_hashes_;

    // LRU 链表：front = 最新，back = 最旧
    std::list<std::string> lru_list_;
    // uid -> LRU 链表中的迭代器
    std::unordered_map<std::string, std::list<std::string>::iterator> lru_map_;
};

} // namespace transfer_queue
