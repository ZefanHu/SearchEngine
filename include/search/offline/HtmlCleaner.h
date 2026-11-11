#pragma once

#include <string>
#include <string_view>

namespace search {

// 把一段可能含 HTML 的文本清洗成纯文本：
//   1. 去掉 <tag ...> 标签；
//   2. 解码常见实体（&nbsp; &amp; &lt; &gt; &quot; &#NNN; &#xHH;）；
//   3. 把连续空白（含全角空格、不换行空格）折叠成一个空格，并去掉首尾空白。
//
// 手写单趟状态机，O(n)，不分配临时对象。
// 原实现每处理一个字段就现场构造两个 std::regex 并做两次 regex_replace——std::regex 的构造和匹配
// 都非常慢（比手写扫描慢 1~2 个数量级），在百万文档规模下是离线建库最大的 CPU 热点之一。
std::string cleanHtml(std::string_view raw);

}  // namespace search
