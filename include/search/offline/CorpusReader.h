#pragma once
// 语料读取抽象：一次吐出一篇文档的“拉模式”迭代器。
//
// 原实现把所有文档一次性读进 vector<string>，语料多大内存就要多大。
// 流式接口让离线管线的内存占用与语料总量无关，也让“换一种语料格式”只需新增一个子类：
//
//   JsonlCorpusReader —— 换行分隔 JSON (NDJSON)，即 BigQuery `EXPORT DATA` / `bq extract` 的导出格式
//   RssCorpusReader   —— 原项目使用的 RSS XML
//   DirectoryCorpusReader —— 递归扫描目录，按扩展名把文件分派给上面两种 reader

#include <filesystem>
#include <memory>

#include "search/offline/Document.h"

namespace search {

class CorpusReader {
public:
    virtual ~CorpusReader() = default;  // 多态基类必须有虚析构，否则通过基类指针 delete 是未定义行为

    // 读取下一篇文档到 out（复用 out 的缓冲区）；语料读完返回 false
    virtual bool next(RawDocument& out) = 0;
};

// 打开单个语料文件（按扩展名选择解析器）；不认识的扩展名抛 std::runtime_error
std::unique_ptr<CorpusReader> openCorpusFile(const std::filesystem::path& file);

// 打开语料目录（递归）；目录不存在或没有任何可识别的文件时抛 std::runtime_error
std::unique_ptr<CorpusReader> openCorpusDirectory(const std::filesystem::path& directory);

}  // namespace search
