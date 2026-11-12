#include "search/offline/SimHash.h"

#include <cmath>
#include <stdexcept>
#include <string_view>

namespace search {
namespace {

// FNV-1a 逐字节哈希，再过一遍 splitmix64 的收尾混合，让 64 个输出位都充分雪崩。
// SimHash 要求“每一位都近似独立的随机位”，只用 FNV 的话短字符串的高位区分度不够。
std::uint64_t hashTerm(std::string_view term) noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (const char ch : term) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 0x100000001B3ULL;
    }
    hash ^= hash >> 30;
    hash *= 0xBF58476D1CE4E5B9ULL;
    hash ^= hash >> 27;
    hash *= 0x94D049BB133111EBULL;
    hash ^= hash >> 31;
    return hash;
}

// 权重 = 1 + log2(tf)，放大 256 倍后取整。
// 用整数计票而不是直接累加 double：浮点加法的结果与累加顺序、编译选项有关，
// 而整数加法在任何机器上都得到同一个结果——“同一份语料产出逐字节相同的索引”依赖这一点。
std::int64_t termWeight(std::uint32_t frequency) noexcept {
    return std::llround(256.0 * (1.0 + std::log2(static_cast<double>(frequency))));
}

}  // namespace

std::uint64_t computeSimHash(const TermFrequencies& terms) noexcept {
    // 每个特征按权重给 64 个“计票器”投票：哈希的第 i 位是 1 就 +权重，是 0 就 -权重
    std::array<std::int64_t, 64> votes{};
    for (const auto& [term, frequency] : terms) {
        const std::uint64_t hash = hashTerm(term);
        const std::int64_t weight = termWeight(frequency);
        for (int bit = 0; bit < 64; ++bit) {
            votes[bit] += ((hash >> bit) & 1U) != 0 ? weight : -weight;
        }
    }
    // 票数为正的位记 1，得到最终指纹
    std::uint64_t fingerprint = 0;
    for (int bit = 0; bit < 64; ++bit) {
        if (votes[bit] > 0) fingerprint |= std::uint64_t{1} << bit;
    }
    return fingerprint;
}

SimHashIndex::SimHashIndex(int max_distance) : max_distance_(max_distance) {
    if (max_distance < 0 || max_distance >= kBlockCount) {
        throw std::invalid_argument("simhash max_distance must be in [0, 3]");
    }
}

bool SimHashIndex::insertIfUnique(std::uint64_t fingerprint) {
    for (int i = 0; i < kBlockCount; ++i) {
        const auto bucket = tables_[i].find(block(fingerprint, i));
        if (bucket == tables_[i].end()) continue;
        for (const std::uint64_t candidate : bucket->second) {
            if (hammingDistance(fingerprint, candidate) <= max_distance_) return false;
        }
    }
    for (int i = 0; i < kBlockCount; ++i) tables_[i][block(fingerprint, i)].push_back(fingerprint);
    ++size_;
    return true;
}

}  // namespace search
