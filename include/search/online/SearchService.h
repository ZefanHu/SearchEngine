#pragma once
// 在线服务门面（Facade）：拥有全部只读数据与缓存，对外只暴露“输入查询串 → 输出 JSON”两个接口。
//
// 它不依赖任何网络框架：HTTP 服务（wfrest）和命令行工具（search_cli）用的是同一个 SearchService，
// 所以核心逻辑可以脱离网络单独测试，将来换 HTTP 框架也只需要改 src/server 一个目录。
//
// 原项目用 Configuration / Dictionary 两个全局单例把数据“广播”给所有类；这里改为
// 由 SearchService 统一持有，并通过构造函数把引用传给 WebSearcher —— 所有依赖一目了然。
//
// 线程模型：构造完成后，除两个缓存（内部自带分片锁）外所有成员都是只读的，
// search()/recommend() 可被任意多个线程同时调用。

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "search/common/Config.h"
#include "search/common/Tokenizer.h"
#include "search/index/InvertedIndex.h"
#include "search/index/PageStore.h"
#include "search/online/KeyRecommender.h"
#include "search/online/LRUCache.h"
#include "search/online/WebSearcher.h"

namespace search {

class SearchService {
public:
    // 响应体用 shared_ptr<const string> 传递：缓存和正在发送响应的线程共享同一份 JSON，
    // 既不用在锁内拷贝大字符串，也不怕缓存淘汰时把别人正在用的数据释放掉。
    using Response = std::shared_ptr<const std::string>;

    // 查询串长度上限（字节）。超过则拒绝，防止恶意超长输入消耗 CPU/内存
    static constexpr std::size_t kMaxQueryBytes = 256;

    explicit SearchService(const Config& config);

    // 只查缓存，不计算；未命中返回 nullptr。给 HTTP 层在网络线程上做“快速路径”用
    Response cachedSearch(std::string_view query);
    Response cachedRecommend(std::string_view query);

    // 查缓存，未命中则计算并写回缓存。返回值永不为 nullptr。
    //   search    → [{"title": "...", "url": "...", "content": "摘要..."}, ...]
    //   recommend → ["词1", "词2", ...]
    Response search(std::string_view query);
    Response recommend(std::string_view query);

    std::size_t documentCount() const noexcept { return pages_.size(); }

    // 把用户输入规整成缓存 key：去首尾空白、ASCII 转小写、修复非法 UTF-8。
    // "Hello " 与 "hello" 应当命中同一条缓存。
    static std::string normalizeQuery(std::string_view query);

private:
    using Cache = ShardedLruCache<std::string, Response>;

    std::string buildSearchJson(const std::string& query) const;
    std::string buildRecommendJson(const std::string& query) const;

    // 成员按“依赖顺序”声明：C++ 保证按声明顺序构造、逆序析构，
    // 所以 searcher_（持有 tokenizer_/index_ 的引用）必须排在它们后面。
    Config config_;
    Tokenizer tokenizer_;
    PageStore pages_;
    InvertedIndex index_;
    WebSearcher searcher_;
    KeyRecommender recommender_;
    Cache search_cache_;
    Cache recommend_cache_;
};

}  // namespace search
