#include "transfer_queue/server/dedup_filter.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace transfer_queue {

// ============================================================================
// 构造 / 析构
// ============================================================================

DedupFilter::DedupFilter(size_t max_size, double false_positive_rate)
    : max_size_(std::max(max_size, size_t(1)))
    , false_positive_rate_(false_positive_rate) {
    size_t bit_count = optimal_bit_count(max_size_, false_positive_rate_);
    bloom_num_hashes_ = optimal_hash_count(bit_count, max_size_);
    bloom_bits_.resize(bit_count, false);
}

DedupFilter::~DedupFilter() = default;

// ============================================================================
// 公共方法
// ============================================================================

bool DedupFilter::contains(const std::string& uid) const {
    // 快速路径：bloom filter 说不存在 → 一定不存在
    if (!bloom_maybe_contains(uid)) {
        return false;
    }
    // 慢速路径：精确集合确认
    return lru_map_.count(uid) > 0;
}

bool DedupFilter::insert(const std::string& uid) {
    // 已存在：更新 LRU 位置（提升到最新）
    auto it = lru_map_.find(uid);
    if (it != lru_map_.end()) {
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
        return false;  // 重复
    }

    // 新增：插入到 LRU 头部
    lru_list_.push_front(uid);
    lru_map_[uid] = lru_list_.begin();
    bloom_add(uid);

    // 超出容量：淘汰最旧的
    if (lru_map_.size() > max_size_) {
        evict_oldest();
    }

    return true;  // 新插入
}

void DedupFilter::clear() {
    lru_list_.clear();
    lru_map_.clear();
    std::fill(bloom_bits_.begin(), bloom_bits_.end(), false);
}

size_t DedupFilter::size() const {
    return lru_map_.size();
}

double DedupFilter::current_false_positive_rate() const {
    // 实际误判率公式：(1 - e^(-kn/m))^k
    if (bloom_bits_.empty() || lru_map_.empty()) {
        return 0.0;
    }
    double m = static_cast<double>(bloom_bits_.size());
    double n = static_cast<double>(lru_map_.size());
    double k = static_cast<double>(bloom_num_hashes_);
    return std::pow(1.0 - std::exp(-k * n / m), k);
}

size_t DedupFilter::bloom_bit_count() const {
    return bloom_bits_.size();
}

size_t DedupFilter::bloom_hash_count() const {
    return bloom_num_hashes_;
}

// ============================================================================
// Bloom filter 内部实现
// ============================================================================

size_t DedupFilter::optimal_bit_count(size_t expected_elements, double fpr) {
    // m = -n * ln(p) / (ln(2))^2
    if (expected_elements == 0 || fpr <= 0.0 || fpr >= 1.0) {
        return 1024;  // 合理的最小值
    }
    double m = -static_cast<double>(expected_elements) * std::log(fpr)
               / (std::log(2.0) * std::log(2.0));
    return std::max(static_cast<size_t>(m), size_t(64));
}

size_t DedupFilter::optimal_hash_count(size_t bit_count, size_t expected_elements) {
    // k = (m/n) * ln(2)
    if (expected_elements == 0) {
        return 1;
    }
    double k = (static_cast<double>(bit_count) / static_cast<double>(expected_elements))
               * std::log(2.0);
    return std::max(static_cast<size_t>(k), size_t(1));
}

size_t DedupFilter::bloom_hash(const std::string& uid, size_t i) const {
    // 双重哈希：h(uid, i) = (h1 + i * h2) % m
    // h1 = std::hash, h2 = FNV-1a 变体
    size_t h1 = std::hash<std::string>{}(uid);

    // FNV-1a
    size_t h2 = 14695981039346656037ULL;
    for (char c : uid) {
        h2 ^= static_cast<size_t>(c);
        h2 *= 1099511628211ULL;
    }

    return (h1 + i * h2) % bloom_bits_.size();
}

void DedupFilter::bloom_add(const std::string& uid) {
    for (size_t i = 0; i < bloom_num_hashes_; ++i) {
        bloom_bits_[bloom_hash(uid, i)] = true;
    }
}

bool DedupFilter::bloom_maybe_contains(const std::string& uid) const {
    for (size_t i = 0; i < bloom_num_hashes_; ++i) {
        if (!bloom_bits_[bloom_hash(uid, i)]) {
            return false;
        }
    }
    return true;
}

void DedupFilter::bloom_rebuild() {
    std::fill(bloom_bits_.begin(), bloom_bits_.end(), false);
    for (const auto& uid : lru_list_) {
        bloom_add(uid);
    }
}

// ============================================================================
// LRU 内部实现
// ============================================================================

void DedupFilter::evict_oldest() {
    // 淘汰直到 size <= max_size
    // 淘汰后需要重建 bloom filter（因为 bloom filter 不支持删除）
    bool need_rebuild = false;
    while (lru_map_.size() > max_size_) {
        const auto& oldest = lru_list_.back();
        lru_map_.erase(oldest);
        lru_list_.pop_back();
        need_rebuild = true;
    }
    if (need_rebuild) {
        bloom_rebuild();
    }
}

} // namespace transfer_queue
