#pragma once
// HTTP 接入层（基于 Sogou Workflow + wfrest）。这是整个项目里唯一依赖网络框架的地方，
// 它只做三件事：解析参数 → 调 SearchService → 写响应，不含任何业务逻辑。
//
// 线程模型：
//   - 网络线程（poller/handler）：解析请求、查缓存。命中则直接返回，全程不离开网络线程。
//   - 计算线程（WFGoTask 线程池）：缓存未命中时，把 分词+检索+打分 丢到这里执行，
//     网络线程立刻回去处理下一个连接，不会被一次慢查询阻塞。
//
//   GET /                     首页
//   GET /search?query=xxx     网页检索
//   GET /recommend?query=xxx  关键词联想

#include <wfrest/HttpServer.h>

#include "search/common/Config.h"
#include "search/online/SearchService.h"

namespace search {

class HttpServer {
public:
    // 保证进程的 fd 上限装得下 max_connections 个连接：软上限不够就提高，硬上限也不够则报错并返回 false。
    // 必须在进程里第一次用到 Workflow 之前调用（main 的开头）：Workflow 在初始化 poller 时
    // 按“当时的”fd 软上限分配它的 fd 表，之后再提高上限，超出旧上限的 fd 仍然无法被它管理。
    static bool ensureFdLimit(const Config& config);

    // service 的生命周期必须长于 HttpServer（由 main 保证）
    HttpServer(const Config& config, SearchService& service);

    // 启动并阻塞，直到收到 SIGINT / SIGTERM 后优雅退出。返回进程退出码。
    int run();

private:
    enum class Endpoint { Search, Recommend };

    void registerRoutes();
    void handleQuery(Endpoint endpoint, const wfrest::HttpReq* request, wfrest::HttpResp* response,
                     SeriesWork* series);

    Config config_;
    SearchService& service_;
    wfrest::HttpServer server_;
};

}  // namespace search
