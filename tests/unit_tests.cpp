// 零依赖的单元测试：覆盖所有“纯算法/数据结构”模块（不需要 jieba 词典，不需要网络）。
// 运行：cd build && ctest --output-on-failure      或直接执行 ./build/unit_tests
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "search/common/MappedFile.h"
#include "search/common/UrlCodec.h"
#include "search/common/Utf8.h"
#include "search/index/InvertedIndex.h"
#include "search/index/PageStore.h"
#include "search/offline/Document.h"
#include "search/offline/HtmlCleaner.h"
#include "search/offline/IndexBuilder.h"
#include "search/offline/SimHash.h"
#include "search/online/KeyRecommender.h"
#include "search/online/LRUCache.h"
#include "search/online/TrieTree.h"

namespace {

// ------------------------------ 迷你测试框架 ------------------------------
struct TestCase {
    const char* name;
    std::function<void()> body;
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

std::atomic<int> g_failures{0};  // 并发测试里多个线程都可能报告失败

struct Registrar {
    Registrar(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

#define TEST(name)                                         \
    void test_##name();                                    \
    const Registrar registrar_##name(#name, test_##name);  \
    void test_##name()

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            ++g_failures;                                                                 \
            std::cerr << "    FAILED " << __FILE__ << ":" << __LINE__ << "  " #condition "\n"; \
        }                                                                                 \
    } while (false)

namespace fs = std::filesystem;
using namespace search;

fs::path tempDir() {
    const fs::path dir = fs::temp_directory_path() / "search_engine_unit_tests";
    fs::create_directories(dir);
    return dir;
}

// ---------------------------------- UTF-8 ----------------------------------
TEST(utf8_decode) {
    CHECK(utf8::charLength('a') == 1);
    CHECK(utf8::charLength(0xE4) == 3);
    CHECK(utf8::charLength(0x80) == 1);  // 孤立的续字节也必须能前进
    CHECK(utf8::decode("a中🙂") == U"a中🙂");
    CHECK(utf8::decode("\xE4\xB8") == std::u32string(2, utf8::kReplacement));  // 被截断的汉字
    CHECK(utf8::isValid("搜索 engine"));
    CHECK(!utf8::isValid("\xFF\xFE"));

    std::string encoded;
    for (const char32_t cp : std::u32string(U"a中🙂")) utf8::append(encoded, cp);
    CHECK(encoded == "a中🙂");
}

TEST(utf8_align) {
    const std::string text = "中文";  // 6 字节
    CHECK(utf8::alignForward(text, 1) == 3);
    CHECK(utf8::alignBackward(text, 5) == 3);
    CHECK(utf8::alignBackward(text, 99) == 6);
}

TEST(url_decode) {
    CHECK(urlDecode("%E4%B8%AD+a") == std::optional<std::string>("中 a"));
    CHECK(urlDecode("c%2B%2B") == std::optional<std::string>("c++"));
    CHECK(!urlDecode("%zz"));   // 原实现在这里 stoi 抛异常，进程崩溃
    CHECK(!urlDecode("abc%4"));
    CHECK(!urlDecode("%"));
}

// -------------------------------- HTML 清洗 --------------------------------
TEST(html_cleaner) {
    CHECK(cleanHtml("<p style=\"x\">你好</p> <b>世界</b>") == "你好 世界");
    CHECK(cleanHtml("a&nbsp;&nbsp;b &amp; c&lt;d&#65;&#x4E2D;") == "a b & c<dA中");
    CHECK(cleanHtml("  \t\n　　全角空格　 结尾  ") == "全角空格 结尾");
    CHECK(cleanHtml("1 < 2 && 3 > 2") == "1 < 2 && 3 > 2");  // 不是标签的 < 和 & 原样保留
    CHECK(cleanHtml("<unclosed tag").empty() == false);
    CHECK(cleanHtml("").empty());
}

TEST(term_frequencies) {
    const TermFrequencies terms = countTermFrequencies({"b", "a", "b", "c", "b"});
    CHECK(terms.size() == 3);
    CHECK(terms[0] == (std::pair<std::string, std::uint32_t>("a", 1)));
    CHECK(terms[1] == (std::pair<std::string, std::uint32_t>("b", 3)));
}

// --------------------------------- SimHash ---------------------------------
TEST(simhash_fingerprint) {
    TermFrequencies doc;
    for (int i = 0; i < 200; ++i) doc.emplace_back("term" + std::to_string(i), 1 + i % 5);
    TermFrequencies near = doc;
    near[7].second += 1;  // 只改动一个词的词频
    TermFrequencies other;
    for (int i = 0; i < 200; ++i) other.emplace_back("word" + std::to_string(i * 7), 1 + i % 3);

    CHECK(computeSimHash(doc) == computeSimHash(doc));
    CHECK(hammingDistance(computeSimHash(doc), computeSimHash(near)) <= 3);
    CHECK(hammingDistance(computeSimHash(doc), computeSimHash(other)) > 10);
}

// 回归测试（来自真实语料的误杀）：两篇内容完全不同的短文档，只是都被同一个高频词主导
// （例如成绩表和代码里的大量 "0"）。按原始词频加权时它们的指纹几乎相同；次线性权重下不应判重。
TEST(simhash_not_dominated_by_one_frequent_term) {
    TermFrequencies footballer{{"0", 30}};  // 30 个 "0" + 40 个各出现一次的词
    TermFrequencies code_question{{"0", 18}};
    for (int i = 0; i < 40; ++i) footballer.emplace_back("球员词" + std::to_string(i), 1);
    for (int i = 0; i < 40; ++i) code_question.emplace_back("code" + std::to_string(i), 1);
    std::sort(footballer.begin(), footballer.end());
    std::sort(code_question.begin(), code_question.end());
    CHECK(hammingDistance(computeSimHash(footballer), computeSimHash(code_question)) > 10);

    // 反过来，真正的转载（正文相同，末尾多一句署名）仍然必须判重
    TermFrequencies article;
    for (int i = 0; i < 300; ++i) article.emplace_back("词" + std::to_string(1000 + i), 1 + i % 4);
    TermFrequencies repost = article;
    repost.emplace_back("责任编辑", 1);
    std::sort(repost.begin(), repost.end());
    CHECK(hammingDistance(computeSimHash(article), computeSimHash(repost)) <= 3);
}

// 性质测试：分块索引的判重结果必须与 O(N^2) 暴力比较完全一致
TEST(simhash_index_matches_brute_force) {
    std::mt19937_64 random(42);
    SimHashIndex index(3);
    std::vector<std::uint64_t> accepted;

    for (int i = 0; i < 3000; ++i) {
        std::uint64_t fingerprint = random();
        if (i % 3 == 0 && !accepted.empty()) {  // 三分之一的样本是“已有指纹翻转 0~5 位”
            fingerprint = accepted[random() % accepted.size()];
            for (int flips = static_cast<int>(random() % 6); flips > 0; --flips) fingerprint ^= 1ULL << (random() % 64);
        }
        bool duplicate = false;
        for (const std::uint64_t other : accepted) {
            if (hammingDistance(fingerprint, other) <= 3) {
                duplicate = true;
                break;
            }
        }
        const bool inserted = index.insertIfUnique(fingerprint);
        if (inserted == duplicate) {
            CHECK(inserted != duplicate);
            return;
        }
        if (inserted) accepted.push_back(fingerprint);
    }
    CHECK(index.size() == accepted.size());
}

// -------------------------------- 编辑距离 --------------------------------
TEST(edit_distance) {
    CHECK(editDistance(U"kitten", U"sitting") == 3);
    CHECK(editDistance(U"", U"abc") == 3);
    CHECK(editDistance(U"搜索引擎", U"搜素引擎") == 1);  // 按“字符”而不是“字节”计
    CHECK(editDistance(U"same", U"same") == 0);
}

// ---------------------------------- Trie ----------------------------------
TEST(trie_prefix_range) {
    const std::vector<std::u32string> words = {U"he", U"hello", U"help", U"world", U"中国", U"中文"};
    TrieTree trie;
    for (std::uint32_t id = 0; id < words.size(); ++id) trie.insert(words[id], id);

    CHECK(trie.findPrefix(U"").size() == 6);
    CHECK(trie.findPrefix(U"he").begin == 0 && trie.findPrefix(U"he").end == 3);
    CHECK(trie.findPrefix(U"hel").begin == 1 && trie.findPrefix(U"hel").end == 3);
    CHECK(trie.findPrefix(U"中").begin == 4 && trie.findPrefix(U"中").end == 6);
    CHECK(trie.findPrefix(U"x").empty());
    CHECK(trie.findPrefix(U"hellos").empty());
}

TEST(key_recommender) {
    const fs::path dict = tempDir() / "dict.dat";
    {
        std::ofstream out(dict, std::ios::binary);
        out << "search 50\nsea 90\nseason 10\nhello 30\nhelp 80\nheld 5\n搜索 70\n搜索引擎 20\n";
    }
    const KeyRecommender recommender(dict);

    // 前缀补全：按词频从高到低
    CHECK(recommender.recommend("sea", 10) == (std::vector<std::string>{"sea", "search", "season"}));
    CHECK(recommender.recommend("SEA", 2) == (std::vector<std::string>{"sea", "search"}));
    CHECK(recommender.recommend("搜", 10) == (std::vector<std::string>{"搜索", "搜索引擎"}));
    // 纠错兜底："helo" 没有前缀匹配 → 回退到 "hel"，三个候选的编辑距离都是 1，按词频排序
    CHECK(recommender.recommend("helo", 10) == (std::vector<std::string>{"help", "hello", "held"}));
    // 短查询只容忍 1 次编辑："sxa" 回退到 "s" 后，sea(距离 1) 入选，search/season(距离 >= 4) 被排除
    CHECK(recommender.recommend("sxa", 10) == (std::vector<std::string>{"sea"}));
    CHECK(recommender.recommend("zzz", 10).empty());
    CHECK(recommender.recommend("", 10).empty());
    // 超长输入不会栈溢出（原实现的 VLA 在这里崩溃）
    CHECK(recommender.recommend(std::string(100000, 'a'), 10).empty());
}

// ----------------------------------- LRU -----------------------------------
TEST(lru_cache_eviction) {
    LruCache<std::string, int> cache(2);
    cache.put("a", 1);
    cache.put("b", 2);
    CHECK(cache.get("a") == 1);  // a 变成最近使用
    cache.put("c", 3);           // 淘汰 b
    CHECK(!cache.get("b"));
    CHECK(cache.get("a") == 1 && cache.get("c") == 3);
    cache.put("a", 10);          // 覆盖写
    CHECK(cache.get("a") == 10 && cache.size() == 2);

    LruCache<std::string, int> disabled(0);
    disabled.put("x", 1);
    CHECK(!disabled.get("x") && disabled.size() == 0);
}

// 容量为 0 = 关闭缓存：读写都应该是空操作（实现上直接返回、不加锁），分片后也一样
TEST(lru_cache_zero_capacity_is_disabled) {
    ShardedLruCache<std::string, int> cache(0, 4);
    for (int i = 0; i < 100; ++i) cache.put("key" + std::to_string(i), i);
    CHECK(cache.size() == 0);
    for (int i = 0; i < 100; ++i) CHECK(!cache.get("key" + std::to_string(i)));
    cache.put("key1", 1);  // 重复写同一个 key 也不会留下任何东西
    CHECK(!cache.get("key1") && cache.size() == 0);
}

TEST(sharded_lru_cache_concurrency) {
    ShardedLruCache<int, int> cache(1024, 8);
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&cache, t] {
            for (int i = 0; i < 20000; ++i) {
                const int key = (i * 31 + t) % 4096;
                if (const auto value = cache.get(key)) {
                    if (*value != key * 2) ++g_failures;  // 读到的值必须是完整写入的值
                } else {
                    cache.put(key, key * 2);
                }
            }
        });
    }
    for (auto& thread : threads) thread.join();
    CHECK(cache.size() <= 1024);
}

// ------------------------------- MappedFile -------------------------------
// 预读只是把数据提前读进页缓存，不能改变映射到的内容；空文件、提前取消都不能出错
TEST(mapped_file_preload) {
    const fs::path file = tempDir() / "mapped.bin";
    std::string expected;
    for (int i = 0; i < 3'000'000; ++i) expected += static_cast<char>('a' + i % 26);  // 约 3 MB，跨多个 1 MiB 分块
    std::ofstream(file, std::ios::binary) << expected;

    const MappedFile mapped(file);
    std::atomic<bool> cancel{false};
    CHECK(mapped.preload(cancel));       // 完整读完返回 true
    CHECK(mapped.view() == expected);

    cancel = true;
    CHECK(!mapped.preload(cancel));      // 已取消：立刻返回 false
    CHECK(mapped.view() == expected);

    const fs::path empty = tempDir() / "empty.bin";
    std::ofstream(empty, std::ios::binary).flush();
    const MappedFile nothing(empty);
    cancel = false;
    CHECK(nothing.size() == 0 && nothing.preload(cancel));
}

// ------------------------- 网页库 / 倒排索引 读写往返 -------------------------
TEST(page_store_round_trip) {
    const fs::path data = tempDir() / "pages.dat";
    const fs::path offsets = tempDir() / "pages.idx";
    {
        PageStoreWriter writer(data, offsets);
        CHECK(writer.append("http://a", "标题A", "正文 </content> 不需要转义") == 0);
        CHECK(writer.append("http://b", "", "") == 1);
        writer.close();
    }
    const PageStore store(data, offsets);
    CHECK(store.size() == 2);
    CHECK(store.get(0).title == "标题A");
    CHECK(store.get(0).content == "正文 </content> 不需要转义");
    CHECK(store.get(1).url == "http://b" && store.get(1).content.empty());

    bool thrown = false;
    try {
        store.get(2);
    } catch (const std::out_of_range&) {
        thrown = true;
    }
    CHECK(thrown);
}

// 标题加权：长的“主条目”（标题就是查询词）应当排在反复提到这个词的短文前面
TEST(index_builder_title_weight) {
    TermFrequencies main_article{{"红楼梦", 200}};  // 长文：200 次“红楼梦”，外加 2000 个各出现 5 次的其它词
    for (int i = 0; i < 2000; ++i) main_article.emplace_back("词" + std::to_string(10000 + i), 5);
    TermFrequencies short_mention{{"红楼梦", 14}};   // 短文：14 次“红楼梦”，外加 60 个各出现 2 次的其它词
    for (int i = 0; i < 60; ++i) short_mention.emplace_back("村" + std::to_string(100 + i), 2);
    std::sort(main_article.begin(), main_article.end());
    std::sort(short_mention.begin(), short_mention.end());

    const auto weights = [&](double title_weight) {
        const fs::path path = tempDir() / "title_weight.dat";
        IndexBuilder builder;
        builder.addDocument(0, main_article, {"红楼梦"});    // 标题恰好是这个词
        builder.addDocument(1, short_mention, {"村100"});    // 标题是别的词
        builder.write(path, title_weight);
        const InvertedIndex index(path);
        const PostingList postings = index.find("红楼梦")->postings;
        return std::pair<float, float>{postings.begin()[0].weight, postings.begin()[1].weight};
    };

    const auto [main_plain, short_plain] = weights(0.0);
    CHECK(main_plain < short_plain);        // 不加权：短文反而得分更高（这正是要修的问题）
    const auto [main_boosted, short_boosted] = weights(1.0);
    CHECK(main_boosted > short_boosted);    // 加权后：主条目排在前面
    CHECK(main_boosted > 0.70F);            // 单词标题的权重至少约为 1/sqrt(2)，正文里出现得多还会更高
}

TEST(inverted_index_round_trip) {
    const fs::path path = tempDir() / "index.dat";
    IndexBuilder builder;
    builder.addDocument(0, {{"apple", 2}, {"banana", 1}});
    builder.addDocument(1, {{"apple", 1}, {"cherry", 3}});
    builder.addDocument(2, {{"banana", 4}});
    builder.write(path);

    const InvertedIndex index(path);
    CHECK(index.docCount() == 3 && index.termCount() == 3 && index.postingCount() == 5);
    CHECK(!index.find("durian") && !index.find("") && !index.find("applf"));

    const auto apple = index.find("apple");
    CHECK(apple && apple->postings.size() == 2);
    if (apple) {
        CHECK(apple->postings.begin()[0].doc_id == 0 && apple->postings.begin()[1].doc_id == 1);
        CHECK(std::abs(apple->idf - std::log2(3.0 / 3.0 + 1.0)) < 1e-6);
    }

    // 每篇文档的权重向量应当是单位向量：Σ w^2 == 1
    std::vector<double> norm_square(3, 0.0);
    for (const char* term : {"apple", "banana", "cherry"}) {
        // 注意：不能写成 for (... : index.find(term)->postings)，find() 返回的临时 optional
        // 在循环开始前就析构了，range-for 会绑定到悬垂引用上（C++23 才修复这个坑）
        const PostingList postings = index.find(term)->postings;
        for (const Posting& posting : postings) norm_square[posting.doc_id] += posting.weight * posting.weight;
    }
    for (const double value : norm_square) CHECK(std::abs(value - 1.0) < 1e-5);

    // 损坏的文件必须被拒绝，而不是带病运行
    const fs::path broken = tempDir() / "broken.dat";
    {
        std::ofstream out(broken, std::ios::binary);
        out << "this is not an index file, just some bytes to exceed the header size";
    }
    bool thrown = false;
    try {
        const InvertedIndex bad(broken);
    } catch (const std::runtime_error&) {
        thrown = true;
    }
    CHECK(thrown);
}

}  // namespace

int main() {
    for (const TestCase& test : registry()) {
        const int before = g_failures.load();
        test.body();
        std::cout << (g_failures == before ? "[ OK ] " : "[FAIL] ") << test.name << "\n";
    }
    std::cout << registry().size() << " test(s), " << g_failures << " failure(s)\n";
    return g_failures == 0 ? 0 : 1;
}
