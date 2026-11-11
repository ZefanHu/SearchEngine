#pragma once
// 分词器：cppjieba 分词 + 小写归一 + 停用词/标点过滤。
//
// 离线建库与在线查询必须使用“同一套”分词与归一规则，否则会出现
// “索引里是 hello、查询切出来是 Hello”这种永远查不到的 bug（原项目就修过一次）。
// 所以把这三步封装在同一个类里，两边共用。

#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "search/common/Config.h"

namespace cppjieba {
class Jieba;  // 前置声明 + Pimpl：Jieba.hpp 很重，避免它污染所有包含本头文件的编译单元
}

namespace search {

class Tokenizer {
public:
    // 索引词项的最大字节数，超长 token（多半是乱码或 URL）直接丢弃
    static constexpr std::size_t kMaxTermBytes = 64;

    explicit Tokenizer(const Config& config);
    ~Tokenizer();  // 必须在 .cpp 中定义：此处 cppjieba::Jieba 还是不完整类型

    Tokenizer(const Tokenizer&) = delete;
    Tokenizer& operator=(const Tokenizer&) = delete;

    // 线程安全：只读成员 + cppjieba 的 const 接口，可被多个线程并发调用
    std::vector<std::string> tokenize(const std::string& text) const;

    bool isStopWord(const std::string& word) const { return stop_words_.count(word) != 0; }

private:
    std::unique_ptr<cppjieba::Jieba> jieba_;
    std::unordered_set<std::string> stop_words_;
};

// 判断一个 token 是否值得进索引（非空、不超长、不是标点/空白）
bool isIndexableToken(std::string_view token) noexcept;

}  // namespace search
