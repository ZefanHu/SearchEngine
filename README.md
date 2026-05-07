# SearchEngine

A high-performance C++ search engine backend built on top of **Sogou Workflow**
and **Wfrest**. It provides two HTTP endpoints — full-text search over a
preprocessed web page corpus and keyword recommendation by prefix — backed by
an in-process sharded LRU cache and an asynchronous compute pipeline.

---

## Core Features

### Asynchronous Architecture
Compute-heavy work (TF–IDF cosine similarity for `/search`, edit-distance
ranking for `/recommend`) is dispatched to `WFGoTask` running on Workflow's
dedicated `Go` thread pool. Network I/O threads stay free to accept new
connections, and the response is committed inside the task's completion
callback via the request's `SeriesWork`. No blocking `compute → respond`
loop on the listener thread.

### O(m) Prefix Lookup with a Trie
The legacy `map<char, set<int>>` "single-character inverted index" has been
replaced with a UTF-8 aware **prefix Trie** (`include/TrieTree.h`,
`src/common/TrieTree.cpp`). The trie is built in memory once at startup by
walking `_dict_freq_vec`. Each node carries a `std::set<int>` of dictionary
indices, so `searchPrefix(query)` is O(m) in the number of UTF-8 characters
of the query — independent of dictionary size — and returns the candidate
set in a single pointer-chase walk.

### Sharded In-Process LRU Cache
A header-only thread-safe template `search::LRUCache<K, V>` (`include/LRUCache.h`)
combines `std::list<pair<K,V>>` (head = MRU, tail = LRU) with
`std::unordered_map<K, list_iterator>` for O(1) `get` / `put` and
splice-based promotion. The server holds `std::vector<unique_ptr<LRUCache>>`
of **4 shards × 100 entries**; each request hashes its query
(`std::hash<std::string>`) into a shard, eliminating a single-mutex
hotspot. This replaces the previous out-of-process Redis dependency,
removing the network round-trip from the hot path entirely.

### Clean Build & Modular Layout
A single top-level `CMakeLists.txt` produces three targets:
`search_common` (shared business code), `search_offline` (corpus build
tools), and `search_server` (the HTTP service binary).

---

## Project Layout

```
SearchEngine/
├── CMakeLists.txt
├── conf/myconf.conf          # runtime config (port, data paths, dict paths)
├── data/                     # input corpus and generated dict / index files
├── include/                  # public headers
│   ├── Configuration.h  Dictionary.h  TrieTree.h  LRUCache.h
│   ├── KeyRecommend.h   WebSearch.h   NetServer.h SearchEngineServer.h
│   ├── PageLib.h        PageLibPreprocessor.h  DictProducer.h  DirScanner.h
│   ├── SplitTool.h      SplitToolCppJieba.h    WebPage.h
│   ├── nlohmann/        # vendored JSON library
│   └── simhash/         # vendored cppjieba + simhash
├── src/
│   ├── common/    Configuration  Dictionary  SplitToolCppJieba  TrieTree  WebPage
│   ├── offline/   DictProducer  PageLib  PageLibPreprocessor  DirScanner
│   └── server/    main  NetServer  SearchEngineServer  KeyRecommend  WebSearch
└── bin/                      # cmake places search_server here
```

---

## Build

### Dependencies

| Component        | Notes                                          |
| ---------------- | ---------------------------------------------- |
| CMake ≥ 3.10     | `apt install cmake`                            |
| A C++11 compiler | `apt install build-essential` (g++ ≥ 5.4)      |
| OpenSSL headers  | `apt install libssl-dev` (required by workflow)|
| Sogou Workflow   | https://github.com/sogou/workflow — `make && sudo make install` |
| Wfrest           | https://github.com/wfrest/wfrest — `make && sudo make install`  |
| tinyxml2         | `apt install libtinyxml2-dev`                  |

`cppjieba`, `simhash` and `nlohmann/json` are already vendored under
`include/` — no extra setup required.

### Compile

```bash
mkdir build && cd build
cmake ..
make -j4
```

Build artifacts:

- `bin/search_server` — the HTTP service
- `build/libsearch_common.a`
- `build/libsearch_offline.a`

---

## Run

The binary uses **relative paths** to locate the configuration file
(`../conf/myconf.conf`) and static assets (`../data/...`), so it must be
launched from inside `bin/`:

```bash
cd bin
./search_server
```

On a clean start you should see the configuration count, then Wfrest's
route registration:

```
[ 配置加载完成 ] 总项数: 24
[WFREST] GET    /search
[WFREST] GET    /recommend
[WFREST] GET    /
```

The default port is `9000` (configurable in `conf/myconf.conf`).

---

## HTTP API

### `GET /recommend?query=<text>`

Returns up to 10 dictionary entries whose words start with the given query
prefix, ranked by edit distance and word frequency.

```bash
curl -s "http://127.0.0.1:9000/recommend?query=中"
```

```json
["中","中人","中体","中了","中为","中作","中其","中且","中以","中介"]
```

### `GET /search?query=<text>`

Tokenizes the query, filters stop words, scores candidate documents by
cosine similarity over an L2-normalized TF–IDF inverted index, and returns
the top-10 documents.

```bash
curl -s "http://127.0.0.1:9000/search?query=就业"
```

```json
[
  {"title": "...", "url": "...", "content": "..."},
  ...
]
```

### `GET /`

Serves `data/static/index.html` — the bundled demo front-end.

---

## Caching Behavior

Each query is hashed into one of 4 shards. On a miss, the request enqueues
a `WFGoTask`; the task's callback both populates the shard
(`LRUCache::put`) and commits the HTTP response. Subsequent identical
queries hit the cache and skip the Go thread pool entirely — typical
warm-cache latency is under 1 ms.

---

## License

See [LICENSE](LICENSE).
