#include "search/online/KeyRecommender.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <numeric>
#include <stdexcept>

#include "search/common/Log.h"
#include "search/common/Utf8.h"

namespace search {
namespace {

constexpr std::size_t kMaxBackoffChars = 2;  // 纠错时最多从查询串末尾回退 2 个字符
constexpr std::uint32_t kMaxFuzzyCandidates = 4096;  // 回退前缀的候选超过这个数就不做 DP，保护延迟

struct Candidate {
    std::uint32_t word_id;
    std::size_t distance;
};

// 固定容量的 Top-K 选择器：内部是一个“堆顶为当前最差者”的堆。
// 扫描 n 个候选只需 O(n log k) 时间、O(k) 空间，不必把 n 个候选全部排序。
template <typename Better>
class TopK {
public:
    TopK(std::size_t capacity, Better better) : capacity_(capacity), better_(better) { heap_.reserve(capacity); }

    void offer(const Candidate& candidate) {
        if (capacity_ == 0) return;
        if (heap_.size() < capacity_) {
            heap_.push_back(candidate);
            std::push_heap(heap_.begin(), heap_.end(), better_);
        } else if (better_(candidate, heap_.front())) {  // 比当前最差者好才有资格入围
            std::pop_heap(heap_.begin(), heap_.end(), better_);
            heap_.back() = candidate;
            std::push_heap(heap_.begin(), heap_.end(), better_);
        }
    }

    // 取出结果，最优者在前
    std::vector<Candidate> take() {
        std::sort_heap(heap_.begin(), heap_.end(), better_);
        return std::move(heap_);
    }

private:
    std::size_t capacity_;
    Better better_;
    std::vector<Candidate> heap_;
};

}  // namespace

std::size_t editDistance(std::u32string_view lhs, std::u32string_view rhs) {
    if (lhs.size() < rhs.size()) std::swap(lhs, rhs);  // 让 rhs 是较短的那个，DP 数组更小

    // row[j] 表示 lhs 的前 i 个字符 与 rhs 的前 j 个字符 的编辑距离；只保留一行并原地更新
    std::vector<std::size_t> row(rhs.size() + 1);
    std::iota(row.begin(), row.end(), std::size_t{0});

    for (std::size_t i = 1; i <= lhs.size(); ++i) {
        std::size_t diagonal = row[0];  // 即二维表里的 dp[i-1][j-1]
        row[0] = i;
        for (std::size_t j = 1; j <= rhs.size(); ++j) {
            const std::size_t above = row[j];  // dp[i-1][j]
            row[j] = lhs[i - 1] == rhs[j - 1] ? diagonal : 1 + std::min({diagonal, above, row[j - 1]});
            diagonal = above;
        }
    }
    return row[rhs.size()];
}

KeyRecommender::KeyRecommender(const std::filesystem::path& dict_path) {
    std::ifstream input(dict_path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open suggest dictionary: " + dict_path.string());

    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const auto space = line.rfind(' ');
        if (space == std::string::npos || space == 0) continue;

        std::uint32_t frequency = 0;
        const char* const number_begin = line.data() + space + 1;
        const char* const number_end = line.data() + line.size();
        const auto [end, error] = std::from_chars(number_begin, number_end, frequency);
        if (error != std::errc{} || end != number_end) continue;

        line.resize(space);
        utf8::toLowerAscii(line);
        if (!utf8::isValid(line)) continue;  // Trie 依赖“字节序 == 码点序”，非法 UTF-8 会破坏这一前提
        words_.push_back({std::move(line), frequency});
        line.clear();  // 被 move 走的 string 处于“有效但未指定”状态，显式清空后再复用
    }

    // 不信任输入文件已排好序：这里再排序 + 去重，保证 Trie 的插入前提一定成立
    std::sort(words_.begin(), words_.end(), [](const Word& lhs, const Word& rhs) { return lhs.text < rhs.text; });
    words_.erase(std::unique(words_.begin(), words_.end(),
                             [](const Word& lhs, const Word& rhs) { return lhs.text == rhs.text; }),
                 words_.end());

    for (std::size_t id = 0; id < words_.size(); ++id) {
        trie_.insert(utf8::decode(words_[id].text), static_cast<std::uint32_t>(id));
    }
    log::info("suggest dictionary loaded: ", words_.size(), " words, ", trie_.nodeCount(), " trie nodes");
}

std::vector<std::string> KeyRecommender::recommend(std::string_view query, std::size_t limit) const {
    std::string normalized(query);
    utf8::toLowerAscii(normalized);
    std::u32string chars = utf8::decode(normalized);
    if (chars.size() > kMaxQueryChars) chars.resize(kMaxQueryChars);
    if (chars.empty() || limit == 0) return {};

    // 排序规则：编辑距离小者优先 → 词频高者优先 → 词典序（保证结果确定）
    const auto better = [this](const Candidate& lhs, const Candidate& rhs) {
        if (lhs.distance != rhs.distance) return lhs.distance < rhs.distance;
        const auto lhs_frequency = words_[lhs.word_id].frequency;
        const auto rhs_frequency = words_[rhs.word_id].frequency;
        if (lhs_frequency != rhs_frequency) return lhs_frequency > rhs_frequency;
        return lhs.word_id < rhs.word_id;
    };

    // ---- 第一级：前缀补全。区间内的词都以 query 开头，直接按词频取 Top-K ----
    const TrieTree::Range exact = trie_.findPrefix(chars);
    TopK prefix_top(limit, better);
    for (std::uint32_t id = exact.begin; id < exact.end; ++id) prefix_top.offer({id, 0});
    std::vector<Candidate> picked = prefix_top.take();

    // ---- 第二级：纠错兜底 ----
    if (picked.size() < limit && chars.size() >= 2) {
        const std::u32string_view all(chars);
        // 短查询只容忍 1 次编辑（3 个字母错 2 个基本就是另一个词了），长查询容忍 2 次
        const std::size_t max_distance = chars.size() <= 4 ? 1 : 2;
        for (std::size_t dropped = 1; dropped <= kMaxBackoffChars && dropped < chars.size(); ++dropped) {
            const TrieTree::Range range = trie_.findPrefix(all.substr(0, chars.size() - dropped));
            if (range.empty()) continue;
            if (range.size() > kMaxFuzzyCandidates) break;

            TopK fuzzy_top(limit - picked.size(), better);
            for (std::uint32_t id = range.begin; id < range.end; ++id) {
                if (id >= exact.begin && id < exact.end) continue;  // 第一级已经选过
                const std::size_t distance = editDistance(all, utf8::decode(words_[id].text));
                if (distance <= max_distance) fuzzy_top.offer({id, distance});
            }
            const std::vector<Candidate> fuzzy = fuzzy_top.take();
            if (!fuzzy.empty()) {
                picked.insert(picked.end(), fuzzy.begin(), fuzzy.end());
                break;
            }
        }
    }

    std::vector<std::string> result;
    result.reserve(picked.size());
    for (const Candidate& candidate : picked) result.push_back(words_[candidate.word_id].text);
    return result;
}

}  // namespace search
