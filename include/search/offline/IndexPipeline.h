#pragma once
// 离线建库管线：语料 → 清洗 → 分词 → SimHash 去重 → 网页库 + 倒排索引 + 联想词典。
//
// 原项目分三个阶段、三次全量读写磁盘：
//   语料 →[全部读入内存]→ webpage.dat →[逐篇读回、去重]→ unrepeated_webpage.dat →[逐篇读回、分词]→ 索引
// 并且每篇文档被 jieba 切了两次（SimHash 一次、建索引一次）。
//
// 现在是“单趟流式”：每篇文档只读一次、只分词一次，去重通过的文档直接写入最终的网页库。
// 分词是 CPU 密集且彼此独立的，按批并行；去重和写盘必须保持顺序，串行执行：
//
//   ┌────────┐  一批 N 篇  ┌──────────────────────────┐  按原顺序  ┌───────────────────────┐
//   │ Reader │ ──────────► │ 多线程：清洗+分词+指纹    │ ─────────► │ 单线程：去重→写库→建索引│
//   └────────┘             └──────────────────────────┘            └───────────────────────┘

#include <cstddef>
#include <cstdint>

#include "search/common/Config.h"

namespace search {

struct BuildStats {
    std::size_t documents_read = 0;     // 从语料读到的文档数
    std::size_t documents_empty = 0;    // 清洗/分词后没有内容而被丢弃
    std::size_t documents_duplicate = 0;  // 被 SimHash 判为近似重复而丢弃
    std::size_t documents_indexed = 0;  // 最终进入索引
    std::size_t terms = 0;              // 不同词项数
    std::uint64_t postings = 0;         // 倒排记录总数
    std::size_t suggest_words = 0;      // 联想词典词条数
    double seconds = 0.0;
};

class IndexPipeline {
public:
    explicit IndexPipeline(const Config& config) : config_(config) {}

    BuildStats run() const;

private:
    Config config_;
};

}  // namespace search
