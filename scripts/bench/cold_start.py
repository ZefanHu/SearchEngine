#!/usr/bin/env python3
"""冷启动实验：页缓存里没有索引时，重启后第一批未命中查询有多慢？preload_index 能改善多少？

做法（每一轮都重复）：
  1. 把索引文件从页缓存里逐出。用的是 posix_fadvise(POSIX_FADV_DONTNEED)，只作用于这几个文件，
     不是全机的 drop_caches，不影响同一台机器上的其它服务；
  2. 用 mincore() 确认它们确实不在内存里了；
  3. 启动 search_server，服务一就绪就用一个连接顺序发 N 条互不相同的查询（缓存是空的，全部未命中），
     记录每条的往返耗时。

三轮：preload_index = false；preload_index = true 且立刻查询；preload_index = true 且等预读完成后再查询。

用法：
  python3 scripts/bench/cold_start.py --base-conf conf/search.conf
  python3 scripts/bench/cold_start.py --base-conf conf/oracle.conf --queries 300 --cpus 2,3
"""
from __future__ import annotations

import argparse
import ctypes
import http.client
import mmap
import os
import signal
import statistics
import subprocess
import time
import urllib.parse
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
INDEX_FILES = ["pages.dat", "pages.idx", "index.dat"]  # dict.dat 启动时本来就整个读入，不算在内
libc = ctypes.CDLL("libc.so.6", use_errno=True)
libc.mmap.restype = ctypes.c_void_p
libc.mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_long]
libc.mincore.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_ubyte)]
libc.munmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t]


def read_conf(path: Path) -> dict[str, str]:
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    return values


def write_conf(base: Path, name: str, overrides: dict[str, str]) -> Path:
    """从 base 派生一份配置放在同一目录（配置里的相对路径相对于配置文件所在目录）。"""
    lines, seen = [], set()
    for line in base.read_text(encoding="utf-8").splitlines():
        key = line.split("=", 1)[0].strip() if "=" in line and not line.lstrip().startswith("#") else None
        if key in overrides:
            lines.append(f"{key} = {overrides[key]}")
            seen.add(key)
        else:
            lines.append(line)
    lines += [f"{key} = {value}" for key, value in overrides.items() if key not in seen]
    target = base.parent / f"bench-{name}.conf"
    target.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return target


def resident_fraction(files: list[Path]) -> float:
    """这些文件有多大比例的页在页缓存里（mincore）。"""
    page = mmap.PAGESIZE
    resident = total = 0
    for path in files:
        size = path.stat().st_size
        if size == 0:
            continue
        fd = os.open(path, os.O_RDONLY)
        try:
            address = libc.mmap(None, size, mmap.PROT_READ, mmap.MAP_SHARED, fd, 0)
            if address in (None, ctypes.c_void_p(-1).value):
                raise OSError(ctypes.get_errno(), "mmap failed")
            pages = (size + page - 1) // page
            vector = (ctypes.c_ubyte * pages)()
            if libc.mincore(address, size, vector) != 0:
                raise OSError(ctypes.get_errno(), "mincore failed")
            resident += sum(byte & 1 for byte in vector)
            total += pages
            libc.munmap(address, size)
        finally:
            os.close(fd)
    return resident / total if total else 1.0


def evict(files: list[Path]) -> None:
    for path in files:
        fd = os.open(path, os.O_RDONLY)
        try:
            os.fsync(fd)  # 脏页逐不出去，先落盘
            os.posix_fadvise(fd, 0, 0, os.POSIX_FADV_DONTNEED)
        finally:
            os.close(fd)


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(len(ordered) * fraction))]


def run_round(label: str, conf: Path, files: list[Path], queries: list[str], port: int, cpus: str, wait_for_preload: bool) -> None:
    evict(files)
    before = resident_fraction(files)

    command = ["./bin/search_server", str(conf)]
    if cpus:
        command = ["taskset", "-c", cpus] + command
    started = time.monotonic()
    server = subprocess.Popen(command, cwd=REPO_ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        connection = None
        while connection is None:
            try:
                candidate = http.client.HTTPConnection("127.0.0.1", port, timeout=30)
                candidate.request("GET", "/")
                candidate.getresponse().read()
                connection = candidate
            except OSError:
                if server.poll() is not None:
                    raise SystemExit(f"server exited early with code {server.returncode}")
                time.sleep(0.01)
        ready_ms = (time.monotonic() - started) * 1000

        waited = 0.0
        if wait_for_preload:
            wait_started = time.monotonic()
            while resident_fraction(files) < 0.99 and time.monotonic() - wait_started < 120:
                time.sleep(0.25)
            waited = time.monotonic() - wait_started
            connection.close()  # 等待期间连接可能已被服务端的 keep-alive 超时关掉，重新连一次
            connection = http.client.HTTPConnection("127.0.0.1", port, timeout=30)
        at_query = resident_fraction(files)

        latencies = []
        for query in queries:
            begin = time.perf_counter()
            connection.request("GET", "/search?query=" + urllib.parse.quote(query))
            response = connection.getresponse()
            response.read()
            latencies.append((time.perf_counter() - begin) * 1000)
            if response.status != 200:
                raise SystemExit(f"unexpected status {response.status} for {query!r}")
        after = resident_fraction(files)
    finally:
        server.send_signal(signal.SIGINT)
        server.wait(timeout=30)

    first = latencies[:20]
    print(f"| {label} | {before:.1%} | {ready_ms:.0f} | {waited:.1f} | {at_query:.1%} | "
          f"{statistics.median(first):.2f} | {statistics.median(latencies):.2f} | {percentile(latencies, 0.9):.2f} | "
          f"{percentile(latencies, 0.99):.2f} | {max(latencies):.2f} | {sum(latencies) / 1000:.2f} | {after:.1%} |")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--base-conf", type=Path, default=REPO_ROOT / "conf/search.conf")
    parser.add_argument("--queries-file", type=Path, default=Path(__file__).resolve().parent / "queries.txt")
    parser.add_argument("--queries", type=int, default=300, help="每轮发多少条互不相同的查询")
    parser.add_argument("--cpus", default="", help="用 taskset 把服务固定在这些核上，例如 2,3")
    args = parser.parse_args()

    base = args.base_conf.resolve()
    values = read_conf(base)
    index_dir = (base.parent / values["index_dir"]).resolve()
    files = [index_dir / name for name in INDEX_FILES]
    port = int(values.get("port", "9000"))
    queries = list(dict.fromkeys(args.queries_file.read_text(encoding="utf-8").split("\n")))  # 去重并保持顺序
    queries = [query for query in queries if query][: args.queries]

    off = write_conf(base, "preload-off", {"preload_index": "false", "listen_host": "127.0.0.1"})
    on = write_conf(base, "preload-on", {"preload_index": "true", "listen_host": "127.0.0.1"})

    total_mb = sum(path.stat().st_size for path in files) / 2**20
    print(f"index: {index_dir} ({total_mb:.0f} MiB in {len(files)} files); {len(queries)} distinct queries per round\n")
    print("| 配置 | 启动前驻留 | 就绪 (ms) | 等待预读 (s) | 查询开始时驻留 | 前 20 条 p50 (ms) | p50 (ms) | p90 (ms) | p99 (ms) | 最大 (ms) | 全部查完 (s) | 查询结束时驻留 |")
    print("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    run_round("preload_index = false", off, files, queries, port, args.cpus, wait_for_preload=False)
    run_round("preload_index = true，就绪后立刻查询", on, files, queries, port, args.cpus, wait_for_preload=False)
    run_round("preload_index = true，等预读完成再查询", on, files, queries, port, args.cpus, wait_for_preload=True)


if __name__ == "__main__":
    main()
