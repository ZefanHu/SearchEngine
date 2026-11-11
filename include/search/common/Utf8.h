#pragma once
// UTF-8 工具函数（header-only）。
//
// 原项目里 nBytesCode() 在 4 个类中各复制了一份，且对非法字节不做任何边界检查。
// 这里集中成一份实现，所有函数都保证：无论输入多么畸形，都不会越界读、不会死循环。

#include <cstddef>
#include <string>
#include <string_view>

namespace search::utf8 {

inline constexpr char32_t kReplacement = 0xFFFD;  // 非法序列统一解码成 U+FFFD

// 由首字节判断该字符占几个字节。非法首字节（含续字节 10xxxxxx）按 1 处理，保证调用方总能前进。
constexpr std::size_t charLength(unsigned char lead) noexcept {
    if (lead < 0x80) return 1;
    if ((lead >> 5) == 0x06) return 2;
    if ((lead >> 4) == 0x0E) return 3;
    if ((lead >> 3) == 0x1E) return 4;
    return 1;
}

constexpr bool isContinuation(unsigned char byte) noexcept { return (byte & 0xC0) == 0x80; }

// 从 text[pos] 解码一个码点，并把 pos 推进到下一个字符。调用前需保证 pos < text.size()。
inline char32_t decodeNext(std::string_view text, std::size_t& pos) noexcept {
    const auto lead = static_cast<unsigned char>(text[pos]);
    const std::size_t len = charLength(lead);
    if (len == 1) {
        ++pos;
        return lead < 0x80 ? lead : kReplacement;
    }
    if (pos + len > text.size()) {  // 字符被截断
        ++pos;
        return kReplacement;
    }
    char32_t code_point = lead & (0xFF >> (len + 1));
    for (std::size_t i = 1; i < len; ++i) {
        const auto byte = static_cast<unsigned char>(text[pos + i]);
        if (!isContinuation(byte)) {
            ++pos;
            return kReplacement;
        }
        code_point = (code_point << 6) | (byte & 0x3F);
    }
    pos += len;
    return code_point;
}

// 整串解码为码点序列。编辑距离、Trie 都在“字符”而不是“字节”上工作。
inline std::u32string decode(std::string_view text) {
    std::u32string out;
    out.reserve(text.size());
    for (std::size_t pos = 0; pos < text.size();) out.push_back(decodeNext(text, pos));
    return out;
}

// 把一个码点按 UTF-8 编码追加到 out 末尾
inline void append(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x110000) {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        append(out, kReplacement);
    }
}

inline bool isValid(std::string_view text) noexcept {
    for (std::size_t pos = 0; pos < text.size();) {
        if (decodeNext(text, pos) == kReplacement) return false;
    }
    return true;
}

// 原地把 ASCII 大写转小写；多字节字符的每个字节都 >= 0x80，不会被误伤。
inline void toLowerAscii(std::string& text) noexcept {
    for (char& ch : text) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
}

// 把 pos 向后挪到最近的字符起始位置（用于在 mmap 的正文里截取摘要，不能把汉字切成两半）
inline std::size_t alignForward(std::string_view text, std::size_t pos) noexcept {
    while (pos < text.size() && isContinuation(static_cast<unsigned char>(text[pos]))) ++pos;
    return pos;
}

// 把 pos 向前挪到最近的字符起始位置
inline std::size_t alignBackward(std::string_view text, std::size_t pos) noexcept {
    if (pos >= text.size()) return text.size();
    while (pos > 0 && isContinuation(static_cast<unsigned char>(text[pos]))) --pos;
    return pos;
}

}  // namespace search::utf8
