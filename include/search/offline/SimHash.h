#pragma once
// SimHash 指纹 + 基于“分块索引”的近似重复检测。
//
// SimHash 的性质：两篇文档内容越相似，它们 64 位指纹的海明距离越小。
// 判重条件：海明距离 <= max_distance（默认 3）。
//
// 原实现把所有指纹放在 vector 里，每来一篇新文档就和之前所有指纹逐个比较：
// 总复杂度 O(N^2)，100 万文档需要约 5×10^11 次比较，根本跑不完。
//
// 这里用抽屉原理把它降到近似 O(N)：
//   把 64 位指纹切成 4 个 16 位块。若两个指纹最多相差 3 位，那么 3 个不同的位最多落在 3 个块里，
//   必然至少有 1 个块完全相同。于是建 4 张哈希表（第 i 张以第 i 块的值为 key），
//   查重时只需要取出“某一块相同”的候选指纹做精确的海明距离比较。
//   100 万文档时每个桶平均只有 15 个指纹，每篇文档比较约 60 次，而不是 100 万次。

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "search/offline/Document.h"

namespace search {

// 由词频表计算 64 位 SimHash 指纹（特征 = 词项，权重 = 词频）
std::uint64_t computeSimHash(const TermFrequencies& terms) noexcept;

inline int hammingDistance(std::uint64_t lhs, std::uint64_t rhs) noexcept {
    return static_cast<int>(std::bitset<64>(lhs ^ rhs).count());  // 编译为一条 POPCNT 指令
}

class SimHashIndex {
public:
    static constexpr int kBlockCount = 4;
    static constexpr int kBlockBits = 16;

    // max_distance 必须在 [0, 3]：抽屉原理要求 距离 < 块数
    explicit SimHashIndex(int max_distance = 3);

    // 若库中已存在与 fingerprint 海明距离 <= max_distance 的指纹，返回 false（判定为重复，不插入）；
    // 否则插入并返回 true。
    bool insertIfUnique(std::uint64_t fingerprint);

    std::size_t size() const noexcept { return size_; }

private:
    static std::uint16_t block(std::uint64_t fingerprint, int index) noexcept {
        return static_cast<std::uint16_t>(fingerprint >> (index * kBlockBits));
    }

    int max_distance_;
    std::size_t size_ = 0;
    std::array<std::unordered_map<std::uint16_t, std::vector<std::uint64_t>>, kBlockCount> tables_;
};

}  // namespace search
