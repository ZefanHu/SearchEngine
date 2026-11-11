#pragma once
// 关键词联想（搜索框下拉提示）。
//
// 两级策略：
//   1. 前缀补全：Trie 定位到以查询串开头的词典区间，按“词频高者优先”取 Top-K。
//   2. 纠错兜底：前缀结果不足 K 个时（多半是打错了字），把查询串末尾去掉 1~2 个字符再查 Trie，
//      对得到的候选词计算与原查询串的最小编辑距离，按“距离小者优先、词频高者优先”补足。
//      例如输入 "helo"：前缀无结果 → 退到 "hel" → 候选 hello / help / held ... → 只保留编辑距离足够小的。
//
// 原实现的问题：
//   - `auto dict = getWordFrequenceDict();` 每次请求把整个词典 vector 深拷贝一份；
//   - 编辑距离的 DP 表是变长数组 int dp[n][m]（非标准 C++），大小由用户输入决定，
//     超长 query 直接栈溢出 —— 这是一个可远程触发的崩溃；
//   - 通过特化 std::less<pair<...>> 来定制优先队列顺序：给标准库类型特化 std 模板是未定义行为，
//     并且会悄悄改变整个程序里所有 std::set/map<该 pair> 的排序；
//   - 结果状态存在成员变量里，对象不可重入，只能每个请求 new 一个。
// 现在 recommend() 是 const 的、无状态的，一个对象可被所有线程并发调用。

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "search/online/TrieTree.h"

namespace search {

// 最小编辑距离（Levenshtein），按 Unicode 字符计。滚动数组实现：时间 O(|a|·|b|)，空间 O(min(|a|,|b|))。
std::size_t editDistance(std::u32string_view lhs, std::u32string_view rhs);

class KeyRecommender {
public:
    // 查询串最多取前 kMaxQueryChars 个字符参与计算，给 DP 的规模设上限
    static constexpr std::size_t kMaxQueryChars = 32;

    // 从 "词 词频" 文本词典加载并构建 Trie；文件不存在抛 std::runtime_error
    explicit KeyRecommender(const std::filesystem::path& dict_path);

    // 返回最多 limit 个联想词。线程安全。
    std::vector<std::string> recommend(std::string_view query, std::size_t limit) const;

    std::size_t wordCount() const noexcept { return words_.size(); }

private:
    struct Word {
        std::string text;
        std::uint32_t frequency = 0;
    };

    std::vector<Word> words_;  // 按 text 升序，下标即 Trie 中的 word_id
    TrieTree trie_;
};

}  // namespace search
