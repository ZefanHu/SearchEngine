#include "search/online/SearchService.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <vector>

#include "nlohmann/json.hpp"
#include "search/common/Log.h"
#include "search/common/Utf8.h"

namespace search {
namespace {

using Json = nlohmann::json;

// dump 的最后一个参数 replace：遇到非法 UTF-8 时替换为 U+FFFD 而不是抛异常。
// 语料里难免有脏字节，不能让一篇坏文档把整个请求变成 500。
std::string dump(const Json& json) { return json.dump(-1, ' ', false, Json::error_handler_t::replace); }

// 在 text 中查找 term 第一次出现的位置（ASCII 不区分大小写；term 已经是小写）
std::size_t findIgnoreCase(std::string_view text, std::string_view term) {
    const auto it = std::search(text.begin(), text.end(), term.begin(), term.end(), [](char lhs, char rhs) {
        const char lowered = (lhs >= 'A' && lhs <= 'Z') ? static_cast<char>(lhs - 'A' + 'a') : lhs;
        return lowered == rhs;
    });
    return it == text.end() ? std::string_view::npos : static_cast<std::size_t>(it - text.begin());
}

// 生成摘要：围绕“最早出现的查询词”截取一段正文，前后按 UTF-8 字符边界对齐。
// content 是指向 mmap 内存的视图，这里只拷贝最终要返回的那几百字节。
std::string makeSnippet(std::string_view content, const std::vector<std::string>& terms, std::size_t max_bytes) {
    constexpr std::size_t kLeadingContext = 60;  // 命中位置之前保留的上文字节数

    std::size_t hit = std::string_view::npos;
    for (const auto& term : terms) hit = std::min(hit, findIgnoreCase(content, term));

    std::size_t begin = 0;
    if (hit != std::string_view::npos && hit > kLeadingContext) {
        begin = utf8::alignForward(content, hit - kLeadingContext);
    }
    const std::size_t end = utf8::alignBackward(content, std::min(content.size(), begin + max_bytes));

    std::string snippet;
    snippet.reserve(end - begin + 8);
    if (begin > 0) snippet += "…";
    snippet.append(content.substr(begin, end - begin));
    if (end < content.size()) snippet += "…";
    return snippet;
}

}  // namespace

SearchService::SearchService(const Config& config)
    : config_(config),
      tokenizer_(config_),
      pages_(config_.pageDataPath(), config_.pageOffsetPath()),
      index_(config_.invertedIndexPath()),
      searcher_(tokenizer_, index_),
      recommender_(config_.suggestDictPath()),
      search_cache_(config_.cache_capacity, config_.cache_shards),
      recommend_cache_(config_.cache_capacity, config_.cache_shards) {
    if (index_.docCount() != pages_.size()) {
        throw std::runtime_error("index and page store are out of sync, please re-run build_index");
    }
    log::info("search service ready: docs=", pages_.size(), " terms=", index_.termCount(),
              " postings=", index_.postingCount());

    // 线程放在构造函数的最后启动：前面任何一步抛异常时都还没有线程需要回收
    if (config_.preload_index) {
        preload_thread_ = std::thread([this] {
            const auto started = std::chrono::steady_clock::now();
            // 先读倒排索引（每次查询都要用，而且比网页库小），再读网页库（只在生成摘要时用）
            const bool finished = index_.preload(preload_cancel_) && pages_.preload(preload_cancel_);
            const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started;
            log::info("index preload ", finished ? "finished" : "cancelled", " after ", elapsed.count(), "s");
        });
    }
}

SearchService::~SearchService() {
    preload_cancel_.store(true);
    if (preload_thread_.joinable()) preload_thread_.join();
}

std::string SearchService::normalizeQuery(std::string_view query) {
    constexpr std::string_view kBlank = " \t\r\n";
    const auto first = query.find_first_not_of(kBlank);
    if (first == std::string_view::npos) return {};
    const auto last = query.find_last_not_of(kBlank);
    query = query.substr(first, last - first + 1);

    std::string normalized;
    normalized.reserve(query.size());
    for (std::size_t pos = 0; pos < query.size();) {
        const std::size_t start = pos;
        const char32_t code_point = utf8::decodeNext(query, pos);
        if (code_point == utf8::kReplacement) {
            utf8::append(normalized, utf8::kReplacement);
        } else {
            normalized.append(query.substr(start, pos - start));
        }
    }
    utf8::toLowerAscii(normalized);
    return normalized;
}

SearchService::Response SearchService::cachedSearch(std::string_view query) {
    return search_cache_.get(normalizeQuery(query)).value_or(nullptr);
}

SearchService::Response SearchService::cachedRecommend(std::string_view query) {
    return recommend_cache_.get(normalizeQuery(query)).value_or(nullptr);
}

SearchService::Response SearchService::search(std::string_view query) {
    std::string key = normalizeQuery(query);
    if (auto cached = search_cache_.get(key)) return *cached;

    // 计算在锁外进行：两个线程同时未命中同一个 key 时会各算一遍（结果相同，后写覆盖先写）。
    // 这比“持锁计算”好得多——后者会让同一分片上的所有请求排队等待一次检索。
    auto response = std::make_shared<const std::string>(buildSearchJson(key));
    search_cache_.put(std::move(key), response);
    return response;
}

SearchService::Response SearchService::recommend(std::string_view query) {
    std::string key = normalizeQuery(query);
    if (auto cached = recommend_cache_.get(key)) return *cached;

    auto response = std::make_shared<const std::string>(buildRecommendJson(key));
    recommend_cache_.put(std::move(key), response);
    return response;
}

std::string SearchService::buildSearchJson(const std::string& query) const {
    Json array = Json::array();
    if (query.empty() || query.size() > kMaxQueryBytes) return dump(array);

    const SearchResult result = searcher_.search(query, config_.search_top_k);
    for (const SearchHit& hit : result.hits) {
        const PageView page = pages_.get(hit.doc_id);
        array.push_back({{"title", page.title},
                         {"url", page.url},
                         {"content", makeSnippet(page.content, result.terms, config_.snippet_bytes)},
                         {"score", hit.score}});
    }
    return dump(array);
}

std::string SearchService::buildRecommendJson(const std::string& query) const {
    Json array = Json::array();
    if (query.empty() || query.size() > kMaxQueryBytes) return dump(array);

    for (auto& word : recommender_.recommend(query, config_.suggest_top_k)) array.push_back(std::move(word));
    return dump(array);
}

}  // namespace search
