#include "search/common/Tokenizer.h"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "search/common/Log.h"
#include "search/common/Utf8.h"
#include "simhash/cppjieba/Jieba.hpp"

namespace search {
namespace {

// 常见的非 ASCII 标点/空白区段：这些字符开头的 token 没有检索价值
constexpr bool isUnicodePunctuation(char32_t cp) noexcept {
    return (cp >= 0x0080 && cp <= 0x00BF) ||  // Latin-1 符号（含不换行空格 U+00A0）
           (cp >= 0x2000 && cp <= 0x206F) ||  // 通用标点（—— … “ ” 等）
           (cp >= 0x3000 && cp <= 0x303F) ||  // CJK 标点（、。《》及全角空格 U+3000）
           (cp >= 0xFF00 && cp <= 0xFF0F) ||  // 全角 ！＂＃…
           (cp >= 0xFF1A && cp <= 0xFF20) ||  // 全角 ：；＜＝＞？＠
           (cp >= 0xFF3B && cp <= 0xFF40) || (cp >= 0xFF5B && cp <= 0xFF65) || cp == utf8::kReplacement;
}

}  // namespace

bool isIndexableToken(std::string_view token) noexcept {
    if (token.empty() || token.size() > Tokenizer::kMaxTermBytes) return false;
    const auto first = static_cast<unsigned char>(token.front());
    if (first < 0x80) return std::isalnum(first) != 0;
    std::size_t pos = 0;
    return !isUnicodePunctuation(utf8::decodeNext(token, pos));
}

Tokenizer::Tokenizer(const Config& config)
    : jieba_(std::make_unique<cppjieba::Jieba>(config.jieba_dict.string(), config.jieba_hmm.string(),
                                               config.jieba_user_dict.string(), config.jieba_idf.string(),
                                               config.jieba_stop_words.string())) {
    for (const auto& file : config.stop_word_files) {
        std::ifstream input(file);
        if (!input) {
            log::warn("cannot open stop word file, skipped: ", file.string());
            continue;
        }
        std::string word;
        while (input >> word) {  // operator>> 以空白分隔，顺带去掉了 CRLF 里的 '\r'
            utf8::toLowerAscii(word);
            stop_words_.insert(std::move(word));
        }
    }
    log::info("tokenizer ready, stop words: ", stop_words_.size());
}

Tokenizer::~Tokenizer() = default;

std::vector<std::string> Tokenizer::tokenize(const std::string& text) const {
    std::vector<std::string> tokens;
    jieba_->Cut(text, tokens, true);

    // erase-remove 惯用法：一次线性扫描完成过滤。
    // 原实现在循环里对 vector 调 erase(it)，每删一个元素都要搬移后面所有元素，是 O(n^2)。
    for (auto& token : tokens) utf8::toLowerAscii(token);
    const auto useless = [this](const std::string& token) {
        return !isIndexableToken(token) || isStopWord(token);
    };
    tokens.erase(std::remove_if(tokens.begin(), tokens.end(), useless), tokens.end());
    return tokens;
}

}  // namespace search
