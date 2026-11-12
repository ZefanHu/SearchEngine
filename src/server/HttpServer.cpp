#include "search/server/HttpServer.h"

#include <sys/resource.h>

#include <csignal>
#include <exception>
#include <optional>
#include <string>
#include <utility>

#include <workflow/WFFacilities.h>
#include <workflow/WFTaskFactory.h>

#include "search/common/Log.h"
#include "search/common/UrlCodec.h"

namespace search {
namespace {

constexpr const char* kJsonType = "application/json; charset=utf-8";

// 信号处理函数里只能做“异步信号安全”的事情，这里仅唤醒在 run() 中等待的主线程
WFFacilities::WaitGroup g_shutdown(1);
void onSignal(int) { g_shutdown.done(); }

void replyError(wfrest::HttpResp* response, int status, const char* message) {
    response->set_status(status);
    response->headers["Content-Type"] = kJsonType;
    response->String(std::string(R"({"error":")") + message + "\"}");
}

}  // namespace

// 每个 TCP 连接占用一个文件描述符，所以 fd 上限必须不低于 max_connections（再加一点余量）。
//
// 只打告警是不够的。压测里实际观察到的故障（docs/BENCHMARK.md 第 5 节）：fd 上限 1024、5000 个并发连接时，
// 进程的 fd 用完后 accept() 一直失败，多出来的 3997 个连接完成了握手却永远卡在内核的 accept 队列里——
// 客户端不报错、服务日志里也没有任何输出，是一种“无声”的故障。所以这里改成：
//   - 软上限不够、硬上限够：自己把软上限提上去（不需要特权，等价于替用户执行了 ulimit -n）；
//   - 硬上限也不够：明确报错并拒绝启动。启动失败是立刻能看到的；连接无声卡住要等到线上高峰才暴露。
bool HttpServer::ensureFdLimit(const Config& config) {
    constexpr rlim_t kReserved = 64;  // 留给监听 socket、epoll、日志、映射索引时短暂打开的文件等
    const rlim_t needed = static_cast<rlim_t>(config.max_connections) + kReserved;

    rlimit limit{};
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
        log::error("getrlimit(RLIMIT_NOFILE) failed");
        return false;
    }
    if (limit.rlim_cur == RLIM_INFINITY || limit.rlim_cur >= needed) return true;

    if (limit.rlim_max != RLIM_INFINITY && limit.rlim_max < needed) {
        log::error("fd hard limit (", limit.rlim_max, ") is lower than max_connections + ", kReserved, " = ", needed,
                   ". Raise it (`ulimit -Hn`, systemd LimitNOFILE=) or lower max_connections in the config.");
        return false;
    }

    const rlim_t previous = limit.rlim_cur;
    limit.rlim_cur = needed;
    if (setrlimit(RLIMIT_NOFILE, &limit) != 0) {
        log::error("cannot raise fd soft limit from ", previous, " to ", needed);
        return false;
    }
    log::info("fd soft limit raised from ", previous, " to ", needed, " (max_connections=", config.max_connections, ")");
    return true;
}

HttpServer::HttpServer(const Config& config, SearchService& service) : config_(config), service_(service) {
    server_.max_connections(config_.max_connections);
    registerRoutes();
}

void HttpServer::registerRoutes() {
    server_.GET("/", [this](const wfrest::HttpReq*, wfrest::HttpResp* response) {
        response->File(config_.web_index.string());
    });

    // 索引规模，给首页展示用。内容在启动后不变，直接在网络线程上应答
    server_.GET("/stats", [this](const wfrest::HttpReq*, wfrest::HttpResp* response) {
        response->headers["Content-Type"] = kJsonType;
        response->String(service_.statsJson());
    });

    server_.GET("/search", [this](const wfrest::HttpReq* request, wfrest::HttpResp* response, SeriesWork* series) {
        handleQuery(Endpoint::Search, request, response, series);
    });

    server_.GET("/recommend", [this](const wfrest::HttpReq* request, wfrest::HttpResp* response, SeriesWork* series) {
        handleQuery(Endpoint::Recommend, request, response, series);
    });
}

void HttpServer::handleQuery(Endpoint endpoint, const wfrest::HttpReq* request, wfrest::HttpResp* response,
                             SeriesWork* series) {
    // ---- 1. 参数校验（网络线程）----
    std::optional<std::string> query = urlDecode(request->query("query"));
    if (!query) return replyError(response, 400, "malformed percent-encoding in query");
    if (query->empty()) return replyError(response, 400, "missing parameter: query");
    if (query->size() > SearchService::kMaxQueryBytes) return replyError(response, 400, "query too long");

    response->headers["Content-Type"] = kJsonType;

    // ---- 2. 快速路径：缓存命中，直接在网络线程返回 ----
    const SearchService::Response cached =
        endpoint == Endpoint::Search ? service_.cachedSearch(*query) : service_.cachedRecommend(*query);
    if (cached) {
        response->String(*cached);
        return;
    }

    // ---- 3. 慢速路径：把计算交给 Go 线程池 ----
    // series 是这次请求对应的任务串。往里追加一个任务后，框架会等该任务执行完才发送响应，
    // 所以 response 指针在任务执行期间一定有效；同一时刻也只有一个线程在访问它。
    // query 用 init-capture 移动进闭包，避免一次字符串拷贝。
    WFGoTask* task = WFTaskFactory::create_go_task(
        "search-compute", [this, endpoint, response, text = std::move(*query)]() {
            try {
                const SearchService::Response body =
                    endpoint == Endpoint::Search ? service_.search(text) : service_.recommend(text);
                response->String(*body);
            } catch (const std::exception& error) {
                // 异常绝不能逃出任务函数（会导致进程 terminate）；统一转成 500
                log::error("query failed: ", error.what());
                replyError(response, 500, "internal error");
            }
        });
    series->push_back(task);
}

int HttpServer::run() {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    if (server_.start(config_.listen_host.c_str(), config_.port) != 0) {
        log::error("failed to listen on ", config_.listen_host, ":", config_.port, " (address in use or invalid host?)");
        return 1;
    }
    server_.list_routes();
    log::info("listening on http://", config_.listen_host, ":", config_.port,
              "  max_connections=", config_.max_connections, "  (Ctrl+C to stop)");

    g_shutdown.wait();
    log::info("shutting down ...");
    server_.stop();  // 停止接收新连接，并等待进行中的请求处理完
    return 0;
}

}  // namespace search
