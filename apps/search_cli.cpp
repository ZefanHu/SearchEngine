// 命令行查询工具：不启动 HTTP 服务，直接调用 SearchService（与 search_server 完全相同的代码路径）。
//
//   单次查询：search_cli conf/search.conf search    "人工智能"
//             search_cli conf/search.conf recommend "sea"
//   交互/管道：search_cli conf/search.conf           （从标准输入逐行读命令）
//             s <查询串>   网页检索
//             r <查询串>   关键词联想
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

#include "search/common/Config.h"
#include "search/common/Log.h"
#include "search/online/SearchService.h"

namespace {

void run(search::SearchService& service, std::string_view command, std::string_view query) {
    const auto started = std::chrono::steady_clock::now();
    const bool is_search = command == "s" || command == "search";
    const bool is_recommend = command == "r" || command == "recommend";
    if (!is_search && !is_recommend) {
        std::cout << "unknown command: " << command << " (use: s <query> | r <query>)\n";
        return;
    }
    const auto response = is_search ? service.search(query) : service.recommend(query);
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - started).count();
    std::cout << *response << "\n";
    std::cerr << "  (" << micros << " us)\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const search::Config config = search::Config::load(argc > 1 ? argv[1] : "conf/search.conf");
        search::SearchService service(config);

        if (argc >= 4) {
            run(service, argv[2], argv[3]);
            return EXIT_SUCCESS;
        }

        std::string line;
        while (std::getline(std::cin, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            if (line == "q" || line == "quit") break;
            const auto space = line.find(' ');
            const std::string_view view(line);
            run(service, view.substr(0, space), space == std::string::npos ? std::string_view{} : view.substr(space + 1));
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        search::log::error(error.what());
        return EXIT_FAILURE;
    }
}
