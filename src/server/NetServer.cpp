#include "../include/NetServer.h"
#include <workflow/WFTaskFactory.h>
#include <functional>

NetServer::NetServer(int count) : _wait_group(count)
{
    Dictionary::getInstance();

    for (int i = 0; i < 4; ++i)
    {
        _caches.push_back(std::unique_ptr<search::LRUCache<std::string, std::string>>(
            new search::LRUCache<std::string, std::string>(std::stoi(Configuration::getInstance().getConfig("CacheSize")))));
    }
}

NetServer::~NetServer()
{
}

void NetServer::start()
{
    int port = atoi(Configuration::getInstance().getConfig("PORT").c_str());
    if (_server.track().start(port) == 0)
    {
        _server.list_routes();
        _wait_group.wait();
        _server.stop();
    }
    else
    {
        cerr << "[ERROR] : HTTP Server Satrt Failed...\n";
        exit(-1);
    }
}

void NetServer::stop()
{
    _wait_group.done();
}

void NetServer::loadModules()
{
    loadStaticResourceMoudle();
    keyWordRecommendMoudle();
    webPageSearchMoudle();
}

void NetServer::loadStaticResourceMoudle()
{
    _server.GET("/",

                [](const HttpReq *req, HttpResp *resp)
                {
                    resp->File(Configuration::getInstance().getConfig("static_resource"));
                });
}

void NetServer::keyWordRecommendMoudle()
{
    _server.GET("/recommend",

                [this](const HttpReq *req, HttpResp *resp, SeriesWork *series)
                {
                    string encode_uri = req->query("query");
                    string query_word = uriDecode(encode_uri);

                    if (query_word.empty())
                    {
                        resp->String("No query result");
                        return;
                    }

                    // 路由前缀避免与 /search 共享 _caches 时的键碰撞
                    std::string cache_key = "R:" + query_word;

                    // 1. 计算 Hash 确定缓存分片
                    int idx = std::hash<std::string>{}(cache_key) % _caches.size();
                    std::string res;

                    // 2. 查缓存
                    if (_caches[idx]->get(cache_key, res))
                    {
                        resp->String(res);
                        return;
                    }

                    // 3. 缓存未命中：使用 WFGoTask 异步计算，防止阻塞网络线程
                    auto res_ptr = std::make_shared<std::string>();
                    WFGoTask *go_task = WFTaskFactory::create_go_task(
                        "compute_recommend",
                        [query_word, res_ptr]()
                        {
                            KeyRecommend recommend;
                            recommend.doRecommend(query_word);
                            *res_ptr = recommend.getResult();
                        });

                    // 4. 设置计算完成后的回调：写入缓存并返回响应
                    go_task->set_callback(
                        [this, cache_key, resp, idx, res_ptr](WFGoTask *task)
                        {
                            _caches[idx]->put(cache_key, *res_ptr);
                            resp->String(*res_ptr);
                        });

                    // 5. 挂载到 Workflow 的执行流上
                    series->push_back(go_task);
                });
}

void NetServer::webPageSearchMoudle()
{

    _server.GET("/search",

                [this](const HttpReq *req, HttpResp *resp, SeriesWork *series)
                {
                    string encode_uri = req->query("query");
                    string query_word = uriDecode(encode_uri);

                    if (query_word.empty())
                    {
                        resp->String("No query result");
                        return;
                    }

                    // 路由前缀避免与 /recommend 共享 _caches 时的键碰撞
                    std::string cache_key = "S:" + query_word;

                    // 1. 计算 Hash 确定缓存分片
                    int idx = std::hash<std::string>{}(cache_key) % _caches.size();
                    std::string res;

                    // 2. 查缓存
                    if (_caches[idx]->get(cache_key, res))
                    {
                        resp->String(res);
                        return;
                    }

                    // 3. 缓存未命中：使用 WFGoTask 异步计算，防止阻塞网络线程
                    auto res_ptr = std::make_shared<std::string>();
                    WFGoTask *go_task = WFTaskFactory::create_go_task(
                        "compute_search",
                        [query_word, res_ptr]()
                        {
                            WebSearch search;
                            search.doSearch(query_word);
                            *res_ptr = search.getResult();
                        });

                    // 4. 设置计算完成后的回调：写入缓存并返回响应
                    go_task->set_callback(
                        [this, cache_key, resp, idx, res_ptr](WFGoTask *task)
                        {
                            _caches[idx]->put(cache_key, *res_ptr);
                            resp->String(*res_ptr);
                        });

                    // 5. 挂载到 Workflow 的执行流上
                    series->push_back(go_task);
                });
}

string NetServer::uriDecode(const string &encodedURI)
{
    std::ostringstream decoded;
    size_t len = encodedURI.length();

    for (size_t i = 0; i < len; ++i)
    {
        if (encodedURI[i] == '%' && i + 2 < len)
        {
            std::string hex = encodedURI.substr(i + 1, 2);
            char decodedChar = static_cast<char>(std::stoi(hex, nullptr, 16));
            decoded << decodedChar;
            i += 2;
        }
        else if (encodedURI[i] == '+')
        {
            decoded << ' ';
        }
        else
        {
            decoded << encodedURI[i];
        }
    }

    return decoded.str();
}