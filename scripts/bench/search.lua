-- wrk 脚本：每个请求随机选一条查询，URL 编码后请求 /search?query=...
--
--   wrk -t2 -c100 -d30s --latency -s scripts/bench/search.lua http://127.0.0.1:9000 -- scripts/bench/queries.txt
--   wrk ... -s scripts/bench/search.lua http://127.0.0.1:9000 -- scripts/bench/queries.txt fixed
--                                              第二个参数为 fixed 时始终请求第 1 条查询（用来测缓存命中路径）
--
-- wrk 的每个线程有一个独立的 Lua 虚拟机：init/request/response 在各自线程里运行，
-- setup 和 done 在主线程里运行。所以“非 2xx 计数”要放在线程自己的全局变量里，
-- 结束时在 done() 里用 thread:get() 逐个线程取回来求和。

local ENDPOINT = "/search?query="

local threads = {}

function setup(thread)
    thread:set("tid", #threads + 1)
    table.insert(threads, thread)
end

local function urlencode(text)
    return (text:gsub("[^%w%-%._~]", function(c) return string.format("%%%02X", string.byte(c)) end))
end

function init(args)
    non2xx = 0
    local path = args[1] or "scripts/bench/queries.txt"
    local fixed = args[2] == "fixed"

    -- 请求报文在启动时一次性拼好，压测过程中 request() 只做一次随机数 + 查表，
    -- 尽量不让客户端自己成为瓶颈
    requests = {}
    for line in io.lines(path) do
        if #line > 0 then
            requests[#requests + 1] = wrk.format("GET", ENDPOINT .. urlencode(line))
            if fixed then break end
        end
    end
    if #requests == 0 then error("no queries loaded from " .. path) end
    math.randomseed(tid * 7919)  -- 每个线程用不同但固定的种子
end

function request()
    return requests[math.random(#requests)]
end

function response(status)
    if status < 200 or status >= 300 then non2xx = non2xx + 1 end
end

function done(summary, latency)
    local counted = 0
    for _, thread in ipairs(threads) do counted = counted + thread:get("non2xx") end
    local errors = summary.errors
    -- 一行机器可读的结果，run_bench.sh 靠它汇总（时间单位：微秒）
    io.write(string.format(
        "BENCH_RESULT requests=%d duration_us=%d qps=%.1f p50_us=%d p90_us=%d p99_us=%d max_us=%d " ..
        "err_connect=%d err_read=%d err_write=%d err_timeout=%d non2xx=%d non2xx_wrk=%d\n",
        summary.requests, summary.duration, summary.requests / (summary.duration / 1e6),
        latency:percentile(50), latency:percentile(90), latency:percentile(99), latency.max,
        errors.connect, errors.read, errors.write, errors.timeout, counted, errors.status))
end
