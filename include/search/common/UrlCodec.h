#pragma once
// URL 百分号解码（application/x-www-form-urlencoded）。
//
// 原实现用 std::stoi(hex, nullptr, 16) 解码：请求里只要带一个 "%zz"，stoi 就抛 invalid_argument，
// 异常从回调逃逸后整个服务进程直接终止 —— 一条畸形 URL 即可远程打挂服务。
// 这里逐字符校验，任何非法输入都只会得到 std::nullopt（上层转成 400 响应）。

#include <optional>
#include <string>
#include <string_view>

namespace search {

inline std::optional<std::string> urlDecode(std::string_view encoded) {
    const auto hex_value = [](char ch) noexcept -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };

    std::string decoded;
    decoded.reserve(encoded.size());
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        const char ch = encoded[i];
        if (ch == '+') {
            decoded.push_back(' ');
        } else if (ch == '%') {
            if (i + 2 >= encoded.size()) return std::nullopt;  // '%' 后面不足两个字符
            const int high = hex_value(encoded[i + 1]);
            const int low = hex_value(encoded[i + 2]);
            if (high < 0 || low < 0) return std::nullopt;
            decoded.push_back(static_cast<char>((high << 4) | low));
            i += 2;
        } else {
            decoded.push_back(ch);
        }
    }
    return decoded;
}

}  // namespace search
