# SearchEngine

基于 **C++17** 的轻量级搜索引擎：离线建库（清洗 → 分词 → SimHash 去重 → TF-IDF 倒排索引）＋
在线服务（网页检索、关键词联想、分片 LRU 缓存、Workflow/wfrest 异步 HTTP）。

> 本分支 `cpp17-reference` 是对原 C++11 版本的整体重构，定位是“可读、可复现的参考实现”。
> 设计思路、阅读顺序与避坑总结见 **[ARCHITECTURE.md](ARCHITECTURE.md)**。

## 功能

| 模块 | 说明 |
| --- | --- |
| 语料接入 | 流式读取 BigQuery 导出的 NDJSON（`*.jsonl`）与 RSS XML（`*.xml`），按扩展名自动识别 |
| 去重 | 64 位 SimHash 指纹 + 分块索引（抽屉原理），判重从 O(N²) 降到近似 O(N) |
| 倒排索引 | L2 归一化 TF-IDF；二进制定长布局，在线端 `mmap` 后零解析、零拷贝使用 |
| 网页检索 | 多路倒排链求交（AND，空则退化为 OR）＋ 余弦相似度 Top-K ＋ 命中词摘要 |
| 关键词联想 | UTF-8 Trie 前缀补全（按词频）＋ 最小编辑距离纠错兜底 |
| 缓存 | 线程安全、哈希分片的 LRU，缓存值用 `shared_ptr<const string>` 共享 |
| HTTP 服务 | wfrest 路由；缓存命中走网络线程快速路径，未命中交给 `WFGoTask` 计算线程池 |

## 目录结构

```
SearchEngine/
├── CMakeLists.txt
├── ARCHITECTURE.md            # 架构导读（先读这个）
├── conf/search.conf           # 运行配置（相对路径以该文件所在目录为基准）
├── apps/                      # 三个可执行程序的 main()
│   ├── build_index.cpp        #   离线建库
│   ├── search_server.cpp      #   HTTP 服务（需要 workflow + wfrest）
│   └── search_cli.cpp         #   命令行查询（无网络依赖，便于调试）
├── include/search/            # 头文件（接口 + 设计说明注释）
│   ├── common/                #   Config / Log / Utf8 / MappedFile / BinaryWriter / Tokenizer / UrlCodec
│   ├── index/                 #   IndexFormat（磁盘布局）/ PageStore / InvertedIndex
│   ├── offline/               #   CorpusReader / HtmlCleaner / SimHash / IndexBuilder / IndexPipeline
│   ├── online/                #   TrieTree / KeyRecommender / WebSearcher / LRUCache / SearchService
│   └── server/                #   HttpServer
├── src/                       # 与 include/search 一一对应的实现
├── tests/unit_tests.cpp       # 单元测试（零依赖）
├── scripts/gen_mock_corpus.py # 生成 BigQuery 导出格式的模拟语料
├── third_party/               # cppjieba、simhash、nlohmann/json（原样保留，未修改）
├── data/
│   ├── jieba/                 #   分词词典
│   ├── stopwords/             #   停用词
│   ├── corpus/rss/            #   人民网 RSS 样例语料
│   ├── corpus/gcp/            #   模拟/真实的 NDJSON 语料（不入库）
│   └── index/                 #   build_index 的产物（不入库）
└── web/index.html             # 演示前端
```

## 构建

依赖：CMake ≥ 3.16、支持 C++17 的编译器（GCC ≥ 8 / Clang ≥ 7）。

| 依赖 | 用途 | 安装 |
| --- | --- | --- |
| tinyxml2 | 解析 RSS 语料 | `apt install libtinyxml2-dev`；找不到时 CMake 会自动拉取源码 |
| [workflow](https://github.com/sogou/workflow) + [wfrest](https://github.com/wfrest/wfrest) | 仅 `search_server` 需要 | 源码 `make && sudo make install`（另需 `libssl-dev`） |

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure      # 单元测试
```

产物在 `bin/`：`build_index`、`search_cli`，以及（检测到 wfrest 时）`search_server`。
没有安装 workflow/wfrest（例如在 Windows 上）时，CMake 会跳过 `search_server`，其余目标照常构建。

## 运行

```bash
# 1.（可选）生成 BigQuery 导出格式的模拟语料，默认 2 万篇，写入 data/corpus/gcp/
python3 scripts/gen_mock_corpus.py
python3 scripts/gen_mock_corpus.py --docs 1000000     # 百万级

# 2. 离线建库：读取 data/corpus 下所有语料，生成 data/index/
./bin/build_index conf/search.conf

# 3a. 命令行查询（不需要 HTTP 服务）
./bin/search_cli conf/search.conf search "海洋经济"
./bin/search_cli conf/search.conf recommend "中"

# 3b. 启动 HTTP 服务，浏览器打开 http://127.0.0.1:9000/
./bin/search_server conf/search.conf
```

程序可以在任意目录下启动：配置文件里的相对路径都以配置文件所在目录为基准。

### 替换为真实的 BigQuery 数据

```sql
EXPORT DATA OPTIONS (
  uri = 'gs://my-bucket/news/news-*.jsonl', format = 'JSON', overwrite = true
) AS
SELECT url, title, body AS content FROM `my_project.my_dataset.news`;
```

```bash
gsutil -m cp 'gs://my-bucket/news/news-*.jsonl' data/corpus/gcp/
./bin/build_index conf/search.conf
```

只要求每行 JSON 含 `url`、`title`、`content` 三个字符串字段，其余字段会被忽略。

## HTTP API

| 接口 | 说明 | 返回 |
| --- | --- | --- |
| `GET /search?query=<文本>` | 网页检索，最多 `search_top_k` 条 | `[{"title","url","content","score"}, ...]` |
| `GET /recommend?query=<前缀>` | 关键词联想，最多 `suggest_top_k` 条 | `["词1","词2", ...]` |
| `GET /` | 演示页面 | HTML |

参数缺失、超过 256 字节或百分号编码非法时返回 `400` 和 `{"error": "..."}`。

## 部署与高并发

- **监听地址**：`listen_host` 默认 `127.0.0.1`，只允许本机访问；对外由 Caddy/Nginx 反向代理
  （例如 Caddy：`search.example.com { reverse_proxy 127.0.0.1:9000 }`）。确实需要直接对外时改成 `0.0.0.0`。
- **连接数**：`max_connections` 默认 2000（与 Workflow 默认值一致）。压测上万并发连接时，三处要一起调大：
  1. `conf/search.conf` 里的 `max_connections`，例如 `20000`；
  2. 进程文件描述符上限（每个连接占一个 fd）：`ulimit -n 65535`，systemd 服务里对应 `LimitNOFILE=65535`；
  3. 内核全连接队列：`sudo sysctl -w net.core.somaxconn=4096`。

  其中第 2 项 `search_server` 会自己处理：启动时如果 fd 软上限低于 `max_connections + 64`，它会用 `setrlimit`
  把软上限提到所需值；硬上限也不够时打印错误并**拒绝启动**（退出码非 0），需要先调大硬上限
  （`ulimit -Hn`、systemd 的 `LimitNOFILE=`）或调小 `max_connections`。
  这样做是因为 fd 不够时的故障是无声的：多出来的连接会卡在内核的 accept 队列里，客户端和服务日志都没有报错。
- **冷启动**：索引是 `mmap` 的，重启后数据要等第一次被查到才从磁盘读入，页缓存是冷的时候未命中查询要几百毫秒。
  `preload_index = true` 会在启动后用后台线程把索引顺序读进页缓存（不拖慢启动）。读完大约需要
  “索引大小 ÷ 磁盘顺序读速度”，读完之前发起的查询会和它抢磁盘。索引比空闲内存大时不要开。
- **关闭缓存**：`cache_capacity = 0`，此时缓存的读写都直接返回、不加锁。
- 压测方法、各项上限的实测表现见 **[docs/BENCHMARK.md](docs/BENCHMARK.md)**。

```bash
curl -s --get http://127.0.0.1:9000/search    --data-urlencode "query=海洋经济"
curl -s --get http://127.0.0.1:9000/recommend --data-urlencode "query=中"
```

## License

见 [LICENSE](LICENSE)。
