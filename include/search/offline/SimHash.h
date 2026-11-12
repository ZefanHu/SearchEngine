#pragma once
// SimHash 指纹 + 基于“分块索引”的近似重复检测。
//
// SimHash 的性质：两篇文档内容越相似，它们 64 位指纹的海明距离越小。
// 判重条件：海明距离 <= max_distance（默认 3）。
//
// 特征的权重是“次线性”的词频：1 + log2(tf)，而不是 tf 本身。
// 最初用的是原始词频，在真实语料上出了问题：只要某一个词在文档里出现得特别多，它的哈希就几乎单独决定了指纹，
// 于是任何两篇“被同一个高频词主导”的文档都会被判成重复，不管其余内容多么不同。实测的例子：
//   - 一位日本足球运动员的维基条目（成绩表里有 30 个 "0"）被判成与一篇 R 语言的 Stack Overflow 提问重复
//     （代码里有 18 个 "0"），海明距离 1；
//   - 演员条目与电视剧条目因为都有几十个“饰”字被判重；年表类条目因为“年”字被判重。
// 120 万篇语料里这类误杀有几千篇（100 万篇 Stack Overflow 丢了 6229 篇，换成次线性权重后只丢 121 篇）。
// 取对数后，出现 100 次的词权重约 7.6，只是出现 1 次的词的 7.6 倍而不是 100 倍：
// 高频词仍然更重要，但不再能压倒“其它几十上百个词共同投出的票”。
//
// 仍然存在的局限：SimHash 衡量的是“用词的重合度”。由同一个模板生成、只差一两个专有名词的小条目
// （例如维基百科里成批创建的法国市镇、小行星列表的分页）用词几乎完全相同，仍会被判重。
// 这是算法的正常行为，不是 bug；要保留它们只能调小 simhash_max_distance 或按来源关闭去重。
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

// 由词频表计算 64 位 SimHash 指纹（特征 = 词项，权重 = 1 + log2(词频)，见文件头注释）
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
