#!/usr/bin/env bash
# 一键复现 docs/BENCHMARK.md 里的全部压测。
#
#   scripts/bench/run_bench.sh                      # 跑全部阶段
#   scripts/bench/run_bench.sh baseline scale-a     # 只跑指定阶段
#   BASE_CONF=conf/oracle.conf DURATION=30 SCALE_CONNS="10 100" scripts/bench/run_bench.sh scale
#
# 阶段（可任意组合，按书写顺序执行）：
#   baseline       单连接延迟基线：/search 未命中、/search 命中、/recommend
#   scale          = scale-a scale-b scale-c
#   scale-a        场景 A：开缓存 + 固定一条查询（全部命中，测网络层/框架上限）
#   scale-b        场景 B：关缓存 + 随机查询（测纯检索计算）
#   scale-c        场景 C：开缓存 + 随机查询（先预热）
#   c10k-default   场景 C 大连接数；服务端与 wrk 都是 ulimit -n 1024，max_connections=2000（复现瓶颈）
#   c10k-srvlimit  同上，但只限制服务端的 fd，wrk 放开（把“客户端先撞墙”排除掉）
#   c10k-maxconn   fd 都放开，max_connections 仍为 2000（单独观察这一道闸）
#   c10k-tuned     fd 放开 + max_connections=20000（三道闸全部放开）
#
# 环境变量：
#   BASE_CONF     作为模板的配置文件，默认 conf/search.conf（脚本从它派生 conf/bench-*.conf）
#   DURATION      每轮压测秒数，默认 30        GAP     轮与轮之间的间隔秒数，默认 10
#   WARMUP        场景 C 的预热秒数，默认 30   OUT     结果目录，默认 build/bench/<时间戳>
#   SCALE_CONNS   scale-* 的连接数档位         C10K_CONNS / C10K_TUNED_CONNS   c10k-* 的连接数档位
#   SERVER_CPUS / WRK_CPUS   绑核，默认 2,3 和 0,1（客户端与服务端同机时避免互相抢 CPU）
#
# 输出：$OUT/results.tsv（每轮一行）以及每轮的 wrk 原始输出、pidstat 采样、ss -s 快照、服务日志。
#
# 注意：
#   - 脚本不会修改任何 sysctl，只检查 somaxconn / tcp_max_syn_backlog 并在偏小时给出提示。
#   - 开始前会把索引文件顺序读一遍（cat > /dev/null），让它们进入页缓存。索引是 mmap 的，
#     页缓存是冷的时候每次查询都要等磁盘缺页，测到的是磁盘而不是检索代码。WARM_PAGE_CACHE=0 可跳过。

set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$ROOT" || exit 1

BASE_CONF=${BASE_CONF:-conf/search.conf}
DURATION=${DURATION:-30}
GAP=${GAP:-10}
WARMUP=${WARMUP:-30}
OUT=${OUT:-build/bench/$(date +%Y%m%d-%H%M%S)}
SCALE_CONNS=${SCALE_CONNS:-"10 50 100 200 500"}
C10K_CONNS=${C10K_CONNS:-"1000 2000 5000 10000"}
C10K_TUNED_CONNS=${C10K_TUNED_CONNS:-"1000 2000 5000 10000 20000"}
SERVER_CPUS=${SERVER_CPUS:-2,3}
WRK_CPUS=${WRK_CPUS:-0,1}
WRK_THREADS=${WRK_THREADS:-2}
QUERIES=${QUERIES:-scripts/bench/queries.txt}
HIGH_NOFILE=65535
LOW_NOFILE=1024

PHASES=("$@")
[ ${#PHASES[@]} -eq 0 ] && PHASES=(baseline scale c10k-default c10k-srvlimit c10k-maxconn c10k-tuned)

for tool in wrk taskset pidstat ss curl; do
    command -v "$tool" >/dev/null || { echo "missing tool: $tool" >&2; exit 1; }
done
[ -x bin/search_server ] || { echo "bin/search_server not built" >&2; exit 1; }
[ -s "$QUERIES" ] || { echo "$QUERIES not found: run scripts/bench/gen_queries.py first" >&2; exit 1; }

mkdir -p "$OUT"
RESULTS=$OUT/results.tsv
SERVER_PID=""
PORT=$(awk -F= '/^[[:space:]]*port[[:space:]]*=/ {gsub(/[[:space:]]/, "", $2); print $2}' "$BASE_CONF")
PORT=${PORT:-9000}
URL=http://127.0.0.1:$PORT

log() { echo "[$(date +%T)] $*" | tee -a "$OUT/run.log"; }

# ---------------------------------------------------------------------------
# 配置：从 BASE_CONF 派生，只覆盖指定的键。必须放在 conf/ 下，因为配置里的相对路径相对于配置文件所在目录
# ---------------------------------------------------------------------------
make_conf() {  # make_conf <名字> [key=value ...]  → conf/bench-<名字>.conf
    local target=conf/bench-$1.conf pair key value
    shift
    cp "$BASE_CONF" "$target"
    for pair in "$@"; do
        key=${pair%%=*} value=${pair#*=}
        if grep -qE "^[[:space:]]*$key[[:space:]]*=" "$target"; then
            sed -i -E "s|^[[:space:]]*$key[[:space:]]*=.*|$key = $value|" "$target"
        else
            echo "$key = $value" >>"$target"
        fi
    done
    sed -i -E "s|^[[:space:]]*listen_host[[:space:]]*=.*|listen_host = 127.0.0.1|" "$target"  # 压测只走 loopback
    echo "$target"
}

# ---------------------------------------------------------------------------
# 安全检查：有容器变成 unhealthy 就立刻停下（机器上没有 docker 时跳过）
# ---------------------------------------------------------------------------
health_check() {
    command -v docker >/dev/null || return 0
    local bad
    bad=$(docker ps --filter health=unhealthy --format '{{.Names}}' 2>/dev/null)
    if [ -n "$bad" ]; then
        log "ABORT: unhealthy container(s): $bad"
        stop_server
        exit 3
    fi
}

check_sysctl() {
    local somaxconn backlog
    somaxconn=$(sysctl -n net.core.somaxconn) backlog=$(sysctl -n net.ipv4.tcp_max_syn_backlog)
    log "sysctl: net.core.somaxconn=$somaxconn net.ipv4.tcp_max_syn_backlog=$backlog"
    [ "$somaxconn" -lt 4096 ] && log "  hint: sudo sysctl -w net.core.somaxconn=4096   (accept 队列上限；Workflow 以 backlog=4096 调用 listen)"
    [ "$backlog" -lt 4096 ] && log "  hint: sudo sysctl -w net.ipv4.tcp_max_syn_backlog=8192   (半连接队列)"
    return 0
}

# ---------------------------------------------------------------------------
# 服务进程的启动与停止
# ---------------------------------------------------------------------------
start_server() {  # start_server <配置文件> <ulimit -n> <日志名>
    local conf=$1 nofile=$2 tag=$3 i
    SERVER_LOG=$OUT/server-$tag.log
    # 子 shell 里先改 fd 上限再 exec：这样 $! 就是 search_server 自己的 PID
    ( ulimit -n "$nofile" || exit 1; exec taskset -c "$SERVER_CPUS" ./bin/search_server "$conf" ) >"$SERVER_LOG" 2>&1 &
    SERVER_PID=$!
    for i in $(seq 1 150); do
        curl -sf -o /dev/null "$URL/" && return 0
        kill -0 "$SERVER_PID" 2>/dev/null || break
        sleep 0.2
    done
    log "ABORT: server failed to start, see $SERVER_LOG"
    stop_server
    exit 1
}

stop_server() {
    [ -n "$SERVER_PID" ] || return 0
    local i
    if kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -INT "$SERVER_PID"
        for i in $(seq 1 100); do kill -0 "$SERVER_PID" 2>/dev/null || break; sleep 0.2; done
        if kill -0 "$SERVER_PID" 2>/dev/null; then
            log "server did not exit within 20s after SIGINT, sending SIGKILL"
            kill -KILL "$SERVER_PID"
        fi
    fi
    wait "$SERVER_PID" 2>/dev/null
    log "server exit code: $?"
    SERVER_PID=""
}
trap 'stop_server' EXIT
trap 'exit 130' INT TERM

# ---------------------------------------------------------------------------
# 采样用的小工具
# ---------------------------------------------------------------------------
rss_kb() { awk '/^VmRSS:/ {print $2}' "/proc/$1/status" 2>/dev/null || echo 0; }

# /proc/net/netstat 里的累计计数器：accept 队列溢出次数、因此丢弃的连接请求数
tcpext() {
    awk -v want="$1" '$1 == "TcpExt:" { if (!seen) { for (i = 2; i <= NF; i++) col[$i] = i; seen = 1 } else print $(col[want]) }' /proc/net/netstat
}

tcp_mem_pages() { awk '$1 == "TCP:" {for (i = 2; i < NF; i += 2) if ($i == "mem") print $(i + 1)}' /proc/net/sockstat; }

# 从 `pidstat -h` 的输出里按表头名字取一列，输出 "平均值 最大值"
pidstat_column() {  # pidstat_column <文件> <列名>
    awk -v want="$2" '
        /^#/ { for (i = 2; i <= NF; i++) if ($i == want) col = i - 1; next }
        col && NF >= col && $col ~ /^[0-9.]+$/ { sum += $col; n++; if ($col > max) max = $col }
        END { if (n) printf "%.1f %.1f", sum / n, max; else printf "0 0" }' "$1"
}

# ---------------------------------------------------------------------------
# 一轮压测：启动 wrk，同时每秒采样服务端 CPU/RSS，中途抓一次连接数快照，最后往 results.tsv 追加一行
# ---------------------------------------------------------------------------
HEADER="phase\tscenario\tconf\tsrv_nofile\twrk_nofile\tmax_conn\tconns\tthreads\tsecs\trequests\tqps\tp50_ms\tp90_ms\tp99_ms\tmax_ms\terr_connect\terr_read\terr_write\terr_timeout\tnon2xx\tsrv_cpu_avg\tsrv_cpu_peak\twrk_cpu_avg\trss_before_mb\trss_avg_mb\trss_peak_mb\testab_mid\ttcp_mem_before_pg\ttcp_mem_mid_pg\tlisten_overflows\tlisten_drops\tserver_alive\tsrv_fds_mid\taccept_queue_mid"
[ -s "$RESULTS" ] || echo -e "$HEADER" >"$RESULTS"

PHASE="" CONF_TAG="" SRV_NOFILE="" WRK_NOFILE=$HIGH_NOFILE MAX_CONN=""

round() {  # round <场景标签> <连接数> <线程数> <秒数> <lua 脚本> [传给 lua 的额外参数]
    local scenario=$1 conns=$2 threads=$3 secs=$4 lua=$5 extra=${6:-}
    local raw=$OUT/$PHASE-$scenario-c$conns wrk_pid
    health_check
    kill -0 "$SERVER_PID" 2>/dev/null || { log "server is not running, skip $PHASE/$scenario c=$conns"; return 0; }

    local rss_before overflows_before drops_before mem_before
    rss_before=$(rss_kb "$SERVER_PID")
    overflows_before=$(tcpext ListenOverflows) drops_before=$(tcpext ListenDrops) mem_before=$(tcp_mem_pages)

    LC_ALL=C pidstat -h -u -r -p "$SERVER_PID" 1 "$secs" >"$raw.server.pidstat" 2>&1 &
    local sampler=$!
    # shellcheck disable=SC2086
    ( ulimit -n "$WRK_NOFILE" || exit 1
      exec taskset -c "$WRK_CPUS" wrk -t"$threads" -c"$conns" -d"${secs}s" --latency -s "$lua" "$URL" -- "$QUERIES" $extra
    ) >"$raw.wrk.txt" 2>&1 &
    wrk_pid=$!
    LC_ALL=C pidstat -h -u -p "$wrk_pid" 1 "$secs" >"$raw.wrk.pidstat" 2>&1 &
    local wrk_sampler=$!

    sleep $((secs / 2))
    ss -s >"$raw.ss.txt" 2>&1
    local established mem_mid
    established=$(ss -Htn state established "( sport = :$PORT )" 2>/dev/null | wc -l)
    mem_mid=$(tcp_mem_pages)
    # 服务进程实际打开的 fd 数，以及监听 socket 上“已完成握手、但还没被 accept() 取走”的连接数（ss 的 Recv-Q）。
    # estab_mid 是内核视角的已建立连接，包含后者；这两个数对不上时，说明有连接卡在 accept 队列里没人服务。
    local srv_fds accept_queue
    srv_fds=$(ls "/proc/$SERVER_PID/fd" 2>/dev/null | wc -l)
    accept_queue=$(ss -Hltn "( sport = :$PORT )" 2>/dev/null | awk 'NR == 1 {print $2}')

    wait "$wrk_pid"
    wait "$sampler" "$wrk_sampler" 2>/dev/null  # 注意不能写裸的 wait：那会连服务进程一起等

    local alive=yes
    kill -0 "$SERVER_PID" 2>/dev/null || alive=NO

    # wrk 的 done() 打印的 BENCH_RESULT 行 → shell 变量（wrk 自己启动失败时这一行不存在，各项记为 -）
    local requests=- qps=- p50_us=- p90_us=- p99_us=- max_us=- err_connect=- err_read=- err_write=- err_timeout=- non2xx=- pair
    for pair in $(grep -m1 '^BENCH_RESULT' "$raw.wrk.txt" 2>/dev/null | cut -d' ' -f2-); do
        case ${pair%%=*} in
            requests | qps | p50_us | p90_us | p99_us | max_us | err_connect | err_read | err_write | err_timeout | non2xx)
                printf -v "${pair%%=*}" '%s' "${pair#*=}" ;;
        esac
    done
    ms() { if [ "$1" = - ]; then echo -; else awk -v us="$1" 'BEGIN {printf "%.3f", us / 1000}'; fi; }
    mb() { awk -v kb="$1" 'BEGIN {printf "%.1f", kb / 1024}'; }

    local cpu rss wrk_cpu
    read -r -a cpu <<<"$(pidstat_column "$raw.server.pidstat" %CPU)"
    read -r -a rss <<<"$(pidstat_column "$raw.server.pidstat" RSS)"
    read -r -a wrk_cpu <<<"$(pidstat_column "$raw.wrk.pidstat" %CPU)"

    echo -e "$PHASE\t$scenario\t$CONF_TAG\t$SRV_NOFILE\t$WRK_NOFILE\t$MAX_CONN\t$conns\t$threads\t$secs\t$requests\t$qps\t$(ms "$p50_us")\t$(ms "$p90_us")\t$(ms "$p99_us")\t$(ms "$max_us")\t$err_connect\t$err_read\t$err_write\t$err_timeout\t$non2xx\t${cpu[0]}\t${cpu[1]}\t${wrk_cpu[0]}\t$(mb "$rss_before")\t$(mb "${rss[0]}")\t$(mb "${rss[1]}")\t$established\t$mem_before\t$mem_mid\t$(($(tcpext ListenOverflows) - overflows_before))\t$(($(tcpext ListenDrops) - drops_before))\t$alive\t$srv_fds\t${accept_queue:-0}" >>"$RESULTS"
    log "$PHASE/$scenario c=$conns: qps=$qps p50=$(ms "$p50_us")ms p99=$(ms "$p99_us")ms errors(connect/read/write/timeout)=$err_connect/$err_read/$err_write/$err_timeout non2xx=$non2xx cpu=${cpu[0]}% rss_peak=$(mb "${rss[1]}")MB estab=$established srv_fds=$srv_fds accept_queue=${accept_queue:-0} alive=$alive"
    sleep "$GAP"
}

# ---------------------------------------------------------------------------
# 各阶段
# ---------------------------------------------------------------------------
use_server() {  # use_server <配置名> <ulimit -n> [key=value ...]：生成配置并启动服务
    local name=$1 nofile=$2 conf
    shift 2
    conf=$(make_conf "$name" "$@")
    CONF_TAG=$name SRV_NOFILE=$nofile
    MAX_CONN=$(awk -F= '/^[[:space:]]*max_connections[[:space:]]*=/ {gsub(/[[:space:]]/, "", $2); print $2}' "$conf")
    start_server "$conf" "$nofile" "$PHASE-$name-$(date +%H%M%S)"
}

phase_baseline() {
    PHASE=baseline WRK_NOFILE=$HIGH_NOFILE
    use_server nocache $HIGH_NOFILE cache_capacity=0
    round search-miss 1 1 10 scripts/bench/search.lua
    round recommend-nocache 1 1 10 scripts/bench/recommend.lua
    stop_server
    use_server cache $HIGH_NOFILE
    round search-hit 1 1 10 scripts/bench/search.lua fixed
    round recommend-cache 1 1 10 scripts/bench/recommend.lua
    stop_server
}

phase_scale_a() {
    PHASE=scale WRK_NOFILE=$HIGH_NOFILE
    use_server cache $HIGH_NOFILE
    for c in $SCALE_CONNS; do round A-hit "$c" "$WRK_THREADS" "$DURATION" scripts/bench/search.lua fixed; done
    stop_server
}

phase_scale_b() {
    PHASE=scale WRK_NOFILE=$HIGH_NOFILE
    use_server nocache $HIGH_NOFILE cache_capacity=0
    for c in $SCALE_CONNS; do round B-compute "$c" "$WRK_THREADS" "$DURATION" scripts/bench/search.lua; done
    stop_server
}

phase_scale_c() {
    PHASE=scale WRK_NOFILE=$HIGH_NOFILE
    use_server cache $HIGH_NOFILE
    round C-warmup 100 "$WRK_THREADS" "$WARMUP" scripts/bench/search.lua
    for c in $SCALE_CONNS; do round C-mixed "$c" "$WRK_THREADS" "$DURATION" scripts/bench/search.lua; done
    stop_server
}

# 每个连接数档位都重启一次服务并重新预热：这样 RSS 的“压测前基线”干净，才能估算每连接内存
phase_c10k() {  # phase_c10k <阶段名> <服务端 nofile> <wrk nofile> <max_connections> <连接数档位>
    PHASE=$1
    local srv_nofile=$2 wrk_nofile=$3 max_conn=$4 conns=$5 c
    for c in $conns; do
        WRK_NOFILE=$HIGH_NOFILE
        use_server "cache-mc$max_conn" "$srv_nofile" "max_connections=$max_conn"
        round C-warmup 100 "$WRK_THREADS" "$WARMUP" scripts/bench/search.lua
        WRK_NOFILE=$wrk_nofile
        round C-mixed "$c" "$WRK_THREADS" "$DURATION" scripts/bench/search.lua
        grep -h "fd limit" "$SERVER_LOG" | head -1 | sed 's/^/    server log: /' | tee -a "$OUT/run.log"
        stop_server
    done
}

log "results: $RESULTS"
log "machine: $(nproc) cpus, $(uname -m), kernel $(uname -r); $(wrk --version 2>&1 | head -1)"
log "pinning: server on cpu $SERVER_CPUS, wrk on cpu $WRK_CPUS (same host, loopback)"
check_sysctl

if [ "${WARM_PAGE_CACHE:-1}" = 1 ]; then
    INDEX_DIR=$(awk -F= '/^[[:space:]]*index_dir[[:space:]]*=/ {gsub(/[[:space:]]/, "", $2); print $2}' "$BASE_CONF")
    INDEX_DIR=$(cd "$(dirname "$BASE_CONF")" && cd "$INDEX_DIR" && pwd)
    log "warming page cache: $INDEX_DIR ($(du -sh "$INDEX_DIR" | cut -f1))"
    cat "$INDEX_DIR"/* >/dev/null
fi

for phase in "${PHASES[@]}"; do
    log "===== phase: $phase ====="
    case $phase in
        baseline) phase_baseline ;;
        scale) phase_scale_a; phase_scale_b; phase_scale_c ;;
        scale-a) phase_scale_a ;;
        scale-b) phase_scale_b ;;
        scale-c) phase_scale_c ;;
        c10k-default) phase_c10k c10k-default $LOW_NOFILE $LOW_NOFILE 2000 "$C10K_CONNS" ;;
        c10k-srvlimit) phase_c10k c10k-srvlimit $LOW_NOFILE $HIGH_NOFILE 2000 "$C10K_CONNS" ;;
        c10k-maxconn) phase_c10k c10k-maxconn $HIGH_NOFILE $HIGH_NOFILE 2000 "$C10K_CONNS" ;;
        c10k-tuned) phase_c10k c10k-tuned $HIGH_NOFILE $HIGH_NOFILE 20000 "$C10K_TUNED_CONNS" ;;
        *) log "unknown phase: $phase" ;;
    esac
done

log "done. summary:"
column -t -s $'\t' "$RESULTS" | cut -c1-240
