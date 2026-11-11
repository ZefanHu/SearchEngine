#include "search/offline/HtmlCleaner.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>

#include "search/common/Utf8.h"

namespace search {
namespace {

// 尝试把 text 开头的 "&...;" 解码成一个码点。成功返回实体占用的字节数，失败返回 0。
std::size_t decodeEntity(std::string_view text, char32_t& code_point) {
    constexpr std::size_t kMaxEntityBytes = 10;
    const auto semicolon = text.substr(0, kMaxEntityBytes).find(';');
    if (semicolon == std::string_view::npos || semicolon < 2) return 0;
    const std::string_view name = text.substr(1, semicolon - 1);

    if (name.front() == '#') {  // 数字实体：&#160; 或 &#xA0;
        const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
        const std::string_view digits = name.substr(hex ? 2 : 1);
        std::uint32_t value = 0;
        const auto [end, error] =
            std::from_chars(digits.data(), digits.data() + digits.size(), value, hex ? 16 : 10);
        if (error != std::errc{} || end != digits.data() + digits.size() || digits.empty()) return 0;
        code_point = value;
        return semicolon + 1;
    }

    struct Named {
        std::string_view name;
        char32_t code_point;
    };
    static constexpr Named kNamed[] = {{"nbsp", U' '}, {"amp", U'&'},   {"lt", U'<'},     {"gt", U'>'},
                                       {"quot", U'"'}, {"apos", U'\''}, {"ldquo", 0x201C}, {"rdquo", 0x201D},
                                       {"mdash", 0x2014}, {"hellip", 0x2026}};
    for (const auto& entity : kNamed) {
        if (entity.name == name) {
            code_point = entity.code_point;
            return semicolon + 1;
        }
    }
    return 0;
}

// text 开头是否是空白字符；是则返回其字节数（ASCII 空白 1，U+00A0 为 2，U+3000 全角空格为 3）
std::size_t whitespaceLength(std::string_view text) noexcept {
    const auto first = static_cast<unsigned char>(text[0]);
    if (first <= ' ') return 1;  // 所有 ASCII 控制字符与空格一并视作空白
    if (text.size() >= 2 && first == 0xC2 && static_cast<unsigned char>(text[1]) == 0xA0) return 2;
    if (text.size() >= 3 && first == 0xE3 && static_cast<unsigned char>(text[1]) == 0x80 &&
        static_cast<unsigned char>(text[2]) == 0x80) {
        return 3;
    }
    return 0;
}

bool looksLikeTagStart(std::string_view text) noexcept {
    if (text.size() < 2) return false;
    const auto next = static_cast<unsigned char>(text[1]);
    return std::isalpha(next) != 0 || next == '/' || next == '!' || next == '?';
}

}  // namespace

std::string cleanHtml(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    bool pending_space = false;  // 遇到空白先记账，等下一个可见字符出现时再补一个空格

    const auto emit = [&](std::string_view bytes) {
        if (pending_space && !out.empty()) out.push_back(' ');
        pending_space = false;
        out.append(bytes);
    };

    for (std::size_t pos = 0; pos < raw.size();) {
        const std::string_view rest = raw.substr(pos);

        if (rest[0] == '<' && looksLikeTagStart(rest)) {
            const auto close = rest.find('>');
            if (close != std::string_view::npos) {  // 整个标签替换成一个空白，防止相邻两段文字粘连
                pending_space = true;
                pos += close + 1;
                continue;
            }
        }

        if (rest[0] == '&') {
            char32_t code_point = 0;
            if (const std::size_t used = decodeEntity(rest, code_point); used > 0) {
                if (code_point == U' ' || code_point == 0xA0 || code_point == 0x3000) {
                    pending_space = true;
                } else {
                    std::string encoded;
                    utf8::append(encoded, code_point);
                    emit(encoded);
                }
                pos += used;
                continue;
            }
        }

        if (const std::size_t blank = whitespaceLength(rest); blank > 0) {
            pending_space = true;
            pos += blank;
            continue;
        }

        const std::size_t length = std::min(utf8::charLength(static_cast<unsigned char>(rest[0])), rest.size());
        emit(rest.substr(0, length));
        pos += length;
    }
    return out;
}

}  // namespace search
