#include "search/online/WebSearcher.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace search {
namespace {

// 一个查询词对应的倒排链及其在查询向量中的权重
struct Clause {
    PostingList postings;
    float weight = 0.0F;
};

// AND：以最短的倒排链为驱动，逐个 doc_id 到其它链里找。
// 所有链都按 doc_id 升序，所以每条链维护一个只进不退的游标，在 [游标, 末尾) 上二分。
// 复杂度 O(|最短链| · Σ log|其它链|)，远好于把所有链都遍历一遍。
std::vector<SearchHit> intersect(const std::vector<Clause>& clauses) {
    std::vector<SearchHit> hits;
    std::vector<const Posting*> cursors;
    cursors.reserve(clauses.size());
    for (const Clause& clause : clauses) cursors.push_back(clause.postings.begin());

    for (const Posting& driver : clauses.front().postings) {
        float score = clauses.front().weight * driver.weight;
        bool in_all = true;
        for (std::size_t i = 1; i < clauses.size(); ++i) {
            const Posting* const end = clauses[i].postings.end();
            cursors[i] = std::lower_bound(cursors[i], end, driver.doc_id,
                                          [](const Posting& posting, DocId target) { return posting.doc_id < target; });
            if (cursors[i] == end) return hits;  // 某条链已耗尽，后面不可能再有交集
            if (cursors[i]->doc_id != driver.doc_id) {
                in_all = false;
                break;
            }
            score += clauses[i].weight * cursors[i]->weight;
        }
        if (in_all) hits.push_back({driver.doc_id, score});
    }
    return hits;
}

// OR：把每条链上的得分累加到对应文档
std::vector<SearchHit> unite(const std::vector<Clause>& clauses) {
    std::unordered_map<DocId, float> scores;
    scores.reserve(clauses.back().postings.size());  // clauses 已按链长升序，back() 最长
    for (const Clause& clause : clauses) {
        for (const Posting& posting : clause.postings) scores[posting.doc_id] += clause.weight * posting.weight;
    }
    std::vector<SearchHit> hits;
    hits.reserve(scores.size());
    for (const auto& [doc_id, score] : scores) hits.push_back({doc_id, score});
    return hits;
}

}  // namespace

SearchResult WebSearcher::search(const std::string& query, std::size_t top_k) const {
    SearchResult result;
    if (top_k == 0) return result;

    // 1. 分词并统计查询内词频（与离线端完全相同的归一化流程）
    std::vector<std::string> tokens = tokenizer_.tokenize(query);
    std::sort(tokens.begin(), tokens.end());

    std::vector<Clause> clauses;
    bool every_term_found = true;
    double norm_square = 0.0;
    for (auto first = tokens.begin(); first != tokens.end();) {
        const auto last = std::find_if(first, tokens.end(), [&](const std::string& token) { return token != *first; });
        const auto term_frequency = static_cast<float>(last - first);

        if (const auto info = index_.find(*first)) {
            const float weight = term_frequency * info->idf;  // 查询端也用真实 IDF：稀有词更重要
            clauses.push_back({info->postings, weight});
            norm_square += static_cast<double>(weight) * weight;
        } else {
            every_term_found = false;  // 索引里根本没有这个词 → AND 必然为空
        }
        result.terms.push_back(std::move(*first));
        first = last;
    }
    if (clauses.empty()) return result;

    // 2. 查询向量 L2 归一化
    const auto norm = static_cast<float>(std::sqrt(norm_square));
    for (Clause& clause : clauses) clause.weight /= norm;

    // 3. 召回 + 打分
    std::sort(clauses.begin(), clauses.end(),
              [](const Clause& lhs, const Clause& rhs) { return lhs.postings.size() < rhs.postings.size(); });
    if (every_term_found) result.hits = intersect(clauses);
    if (result.hits.empty() && (clauses.size() > 1 || !every_term_found)) result.hits = unite(clauses);

    // 4. Top-K：partial_sort 只保证前 k 个有序，O(n log k)，不必给全部候选排序。
    //    比较函数是严格弱序（得分相同时按 doc_id 决胜），结果完全确定。
    const auto by_score = [](const SearchHit& lhs, const SearchHit& rhs) {
        return lhs.score != rhs.score ? lhs.score > rhs.score : lhs.doc_id < rhs.doc_id;
    };
    const std::size_t keep = std::min(top_k, result.hits.size());
    std::partial_sort(result.hits.begin(), result.hits.begin() + static_cast<std::ptrdiff_t>(keep),
                      result.hits.end(), by_score);
    result.hits.resize(keep);
    return result;
}

}  // namespace search
