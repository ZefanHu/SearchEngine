#include "search/common/Log.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace search::log {

void write(Level level, std::string_view message) {
    static std::mutex mutex;  // 保证多线程下一行日志不被打断
    static constexpr const char* kNames[] = {"INFO ", "WARN ", "ERROR"};

    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char time_text[16] = "--:--:--";

    std::lock_guard lock(mutex);
    if (const std::tm* local = std::localtime(&now)) {  // localtime 非线程安全，放在锁内调用
        std::strftime(time_text, sizeof(time_text), "%H:%M:%S", local);
    }
    std::fprintf(stderr, "[%s] [%s] %.*s\n", time_text, kNames[static_cast<int>(level)],
                 static_cast<int>(message.size()), message.data());
}

}  // namespace search::log
