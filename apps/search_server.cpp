// 在线服务入口：search_server [配置文件路径]
#include <cstdlib>
#include <exception>

#include "search/common/Config.h"
#include "search/common/Log.h"
#include "search/online/SearchService.h"
#include "search/server/HttpServer.h"

int main(int argc, char** argv) {
    try {
        const search::Config config = search::Config::load(argc > 1 ? argv[1] : "conf/search.conf");

        // 对象的构造顺序即依赖顺序：service 先于 server 构造、晚于 server 析构，
        // 保证 server 处理请求期间 service 始终有效。
        search::SearchService service(config);
        search::HttpServer server(config, service);
        return server.run();
    } catch (const std::exception& error) {
        search::log::error("fatal: ", error.what());
        return EXIT_FAILURE;
    }
}
