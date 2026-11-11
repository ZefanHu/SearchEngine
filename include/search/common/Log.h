#pragma once
// 极简线程安全日志：log::info("docs=", n, " cost=", ms, "ms");
// 用 C++17 折叠表达式把任意个参数依次写入流，避免 printf 的格式串/类型不匹配问题
// （原代码用 %ld 打印 size_t，在 32 位或 Windows 上就是未定义行为）。

#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace search::log {

enum class Level { Info, Warn, Error };

void write(Level level, std::string_view message);

template <typename... Args>
std::string concat(Args&&... args) {
    std::ostringstream stream;
    (stream << ... << std::forward<Args>(args));
    return stream.str();
}

template <typename... Args>
void info(Args&&... args) { write(Level::Info, concat(std::forward<Args>(args)...)); }

template <typename... Args>
void warn(Args&&... args) { write(Level::Warn, concat(std::forward<Args>(args)...)); }

template <typename... Args>
void error(Args&&... args) { write(Level::Error, concat(std::forward<Args>(args)...)); }

}  // namespace search::log
