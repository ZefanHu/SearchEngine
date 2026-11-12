-- wrk 脚本：每个请求随机选一条查询，取它的前 2~4 个字母作为前缀，请求 /recommend?query=...
--
--   wrk -t2 -c100 -d30s --latency -s scripts/bench/recommend.lua http://127.0.0.1:9000 -- scripts/bench/queries.txt
--
-- 结构与 search.lua 完全相同（线程模型的说明见 search.lua 顶部注释），只有“怎么从一行查询得到请求路径”不同。

local ENDPOINT = "/recommend?query="

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
    local index = 0
    for line in io.lines(path) do
        local letters = line:gsub("[^%a]", "")  -- 只保留字母，前缀里不出现空格
        if #letters >= 2 then
            index = index + 1
            local length = math.min(#letters, 2 + index % 3)  -- 2、3、4 个字母轮流取，保证可复现
            requests[#requests + 1] = wrk.format("GET", ENDPOINT .. urlencode(letters:sub(1, length):lower()))
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
