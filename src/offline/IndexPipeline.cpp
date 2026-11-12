#include "search/offline/IndexPipeline.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "search/common/Log.h"
#include "search/common/Tokenizer.h"
#include "search/index/PageStore.h"
#include "search/offline/CorpusReader.h"
#include "search/offline/HtmlCleaner.h"
#include "search/offline/IndexBuilder.h"
#include "search/offline/SimHash.h"

namespace search {
namespace {

// 一篇文档经过“并行阶段”后的产物
struct PreparedDocument {
    std::string url;
    std::string title;
    std::string content;
    TermFrequencies terms;
    std::vector<std::string> title_terms;  // 标题里出现的词，升序且不重复
    std::uint64_t fingerprint = 0;
};

// 并行阶段：只读共享的 tokenizer，只写自己的 out，线程之间没有任何共享可变状态，因此不需要锁
void prepare(RawDocument& raw, const Tokenizer& tokenizer, PreparedDocument& out) {
    out.url = std::move(raw.url);
    out.title = cleanHtml(raw.title);
    out.content = cleanHtml(raw.content);

    // 标题和正文分开分词，这样才知道哪些词来自标题（建索引时标题词有额外权重，见 IndexBuilder.h）
    std::vector<std::string> title_tokens = tokenizer.tokenize(out.title);
    std::vector<std::string> tokens = tokenizer.tokenize(out.content);
    tokens.insert(tokens.end(), title_tokens.begin(), title_tokens.end());  // 标题里的词同样应该能被检索到

    out.terms = countTermFrequencies(std::move(tokens));
    out.fingerprint = computeSimHash(out.terms);

    // 标题词表：排序去重，IndexBuilder 用二分查找判断“某个词是否出现在标题里”
    std::sort(title_tokens.begin(), title_tokens.end());
    title_tokens.erase(std::unique(title_tokens.begin(), title_tokens.end()), title_tokens.end());
    out.title_terms = std::move(title_tokens);
}

// 把 [0, count) 分给 thread_count 个线程执行 task(i)。
// 用一个原子计数器做“动态抢任务”：文档长短不一，静态均分会让有的线程早早空闲。
template <typename Task>
void parallelFor(std::size_t count, unsigned thread_count, Task&& task) {
    if (thread_count <= 1 || count <= 1) {
        for (std::size_t i = 0; i < count; ++i) task(i);
        return;
    }

    std::atomic<std::size_t> next{0};
    std::exception_ptr first_error;
    std::mutex error_mutex;

    const auto worker = [&] {
        try {
            for (std::size_t i = next.fetch_add(1); i < count; i = next.fetch_add(1)) task(i);
        } catch (...) {
            // 异常不能跨线程传播：逃出线程函数会直接 std::terminate。先存下来，join 之后在主线程重新抛出
            const std::lock_guard lock(error_mutex);
            if (!first_error) first_error = std::current_exception();
            next.store(count);  // 让其它线程尽快收工
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(thread_count - 1);
    for (unsigned i = 1; i < thread_count; ++i) threads.emplace_back(worker);
    worker();  // 主线程也参与干活
    for (auto& thread : threads) thread.join();

    if (first_error) std::rethrow_exception(first_error);
}

}  // namespace

BuildStats IndexPipeline::run() const {
    const auto started = std::chrono::steady_clock::now();
    const unsigned thread_count =
        config_.build_threads != 0 ? config_.build_threads : std::max(1U, std::thread::hardware_concurrency());
    log::info("build started: threads=", thread_count, " batch=", config_.build_batch_size);

    const Tokenizer tokenizer(config_);
    const auto reader = openCorpusDirectory(config_.corpus_dir);
    PageStoreWriter page_writer(config_.pageDataPath(), config_.pageOffsetPath());
    SimHashIndex seen_fingerprints(config_.simhash_max_distance);
    IndexBuilder index_builder;
    BuildStats stats;

    std::vector<RawDocument> raw_batch(config_.build_batch_size);
    std::vector<PreparedDocument> prepared_batch(config_.build_batch_size);
    std::size_t batch_number = 0;

    while (true) {
        // 阶段 1（串行）：读一批。磁盘顺序读本身很快，瓶颈在后面的分词
        std::size_t batch_size = 0;
        while (batch_size < raw_batch.size() && reader->next(raw_batch[batch_size])) ++batch_size;
        if (batch_size == 0) break;
        stats.documents_read += batch_size;

        // 阶段 2（并行）：清洗 + 分词 + 指纹
        parallelFor(batch_size, thread_count,
                    [&](std::size_t i) { prepare(raw_batch[i], tokenizer, prepared_batch[i]); });

        // 阶段 3（串行，保持语料原始顺序）：去重 → 写网页库 → 加入倒排索引。
        // 顺序固定意味着：同一份语料无论用几个线程建库，产出的文件逐字节相同（可复现）。
        for (std::size_t i = 0; i < batch_size; ++i) {
            const PreparedDocument& doc = prepared_batch[i];
            if (doc.terms.empty()) {
                ++stats.documents_empty;
                continue;
            }
            if (!seen_fingerprints.insertIfUnique(doc.fingerprint)) {
                ++stats.documents_duplicate;
                continue;
            }
            const DocId doc_id = page_writer.append(doc.url, doc.title, doc.content);
            index_builder.addDocument(doc_id, doc.terms, doc.title_terms);
        }

        if (++batch_number % 32 == 0) {  // 每 32 批打印一次进度，日志本身不能成为瓶颈
            log::info("progress: read=", stats.documents_read, " indexed=", page_writer.size(),
                      " duplicate=", stats.documents_duplicate);
        }
    }

    page_writer.close();
    log::info("writing inverted index ...");
    index_builder.write(config_.invertedIndexPath(), config_.title_weight_percent / 100.0);
    stats.suggest_words = index_builder.writeSuggestDict(config_.suggestDictPath(), config_.suggest_min_df);

    stats.documents_indexed = page_writer.size();
    stats.terms = index_builder.termCount();
    stats.postings = index_builder.postingCount();
    stats.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return stats;
}

}  // namespace search
