#!/usr/bin/env python3
"""从 NDJSON 语料的标题里抽取压测用的查询串，写入 scripts/bench/queries.txt。

做法：对语料的所有行做蓄水池抽样（固定随机种子，结果可复现），从抽中的标题里
取 2~4 个“连续的纯英文单词”作为一条查询。标题里凑不出 2 个连续英文单词的行会被跳过，
所以先多抽一些再截取前 N 条。

用法：
  python3 scripts/bench/gen_queries.py                       # 默认读 data/corpus/bigquery-full
  python3 scripts/bench/gen_queries.py --corpus DIR --count 5000 --seed 42
"""
from __future__ import annotations

import argparse
import json
import random
import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
WORD = re.compile(r"^[A-Za-z]{2,}$")
EDGE_PUNCT = "\"'`()[]{}<>,.:;!?"


def english_runs(title: str) -> list[list[str]]:
    """把标题按空白切开，返回其中所有“连续纯英文单词”的片段（长度 >= 2）。"""
    runs: list[list[str]] = []
    current: list[str] = []
    for raw in title.split():
        word = raw.strip(EDGE_PUNCT)
        # 单词两侧带标点（逗号、括号等）说明这里是短语边界，片段到此为止
        if WORD.match(word) and word == raw:
            current.append(word)
            continue
        if WORD.match(word) and raw.startswith(word):  # 形如 "word," —— 可以作为片段的最后一个词
            current.append(word)
        if len(current) >= 2:
            runs.append(current)
        current = []
    if len(current) >= 2:
        runs.append(current)
    return runs


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--corpus", type=Path, default=REPO_ROOT / "data/corpus/bigquery-full")
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parent / "queries.txt")
    parser.add_argument("--count", type=int, default=5000)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    rng = random.Random(args.seed)
    files = sorted(p for p in args.corpus.rglob("*") if p.suffix in {".jsonl", ".ndjson", ".json"})
    if not files:
        raise SystemExit(f"no NDJSON files under {args.corpus}")

    # 蓄水池抽样：只读一遍，内存里只保留 2N 行原文
    reservoir_size = args.count * 2
    reservoir: list[bytes] = []
    seen = 0
    for path in files:
        with path.open("rb") as stream:
            for line in stream:
                seen += 1
                if len(reservoir) < reservoir_size:
                    reservoir.append(line)
                else:
                    slot = rng.randrange(seen)
                    if slot < reservoir_size:
                        reservoir[slot] = line
    rng.shuffle(reservoir)

    queries: list[str] = []
    for line in reservoir:
        runs = english_runs(json.loads(line).get("title", ""))
        if not runs:
            continue
        run = rng.choice(runs)
        length = rng.randint(2, min(4, len(run)))
        start = rng.randrange(len(run) - length + 1)
        queries.append(" ".join(run[start : start + length]))
        if len(queries) == args.count:
            break

    args.out.write_text("\n".join(queries) + "\n", encoding="utf-8", newline="\n")
    print(f"scanned {seen} titles in {len(files)} file(s); wrote {len(queries)} queries to {args.out}")


if __name__ == "__main__":
    main()
