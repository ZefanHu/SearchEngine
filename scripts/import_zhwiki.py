#!/usr/bin/env python3
"""把中文维基百科的 parquet 转成 build_index 能读的 NDJSON（与 BigQuery 导出格式一致）。

数据源：Hugging Face 数据集 wikimedia/wikipedia 的中文子集 20231101.zh（CC BY-SA 3.0 / GFDL），
6 个 parquet 文件共 1.6 GiB、138 万篇，字段为 id / url / title / text（text 已去掉维基标记）。

  mkdir -p data/corpus/zhwiki-parquet && cd data/corpus/zhwiki-parquet
  for i in 0 1 2 3 4 5; do
    curl -fLO "https://huggingface.co/datasets/wikimedia/wikipedia/resolve/main/20231101.zh/train-0000$i-of-00006.parquet"
  done

  python3 -m venv .venv && .venv/bin/pip install pyarrow opencc     # 依赖装在虚拟环境里
  .venv/bin/python scripts/import_zhwiki.py                         # 默认抽 20 万篇，写入 data/corpus/zhwiki/

做三件事：
  1. 跳过正文过短的条目（默认少于 200 字）：消歧义页、重定向残留、只有一句话的小条目对检索没有价值；
  2. 在剩下的条目里用固定的随机种子抽 N 篇（同样的输入和参数，输出逐字节相同）；
  3. 繁体转简体。中文维基是繁简混排（抽样统计约 61% 的条目以繁体书写），而 cppjieba 的词典是简体：
     不转换的话，繁体条目会被切成单字，用简体查询也搜不到它们。转换用 OpenCC 的 t2s（逐字转换，不做地区用词替换）。

输出每行一个 JSON 对象，只有 url / title / content 三个字段。
"""
from __future__ import annotations

import argparse
import json
import random
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--parquet-dir", type=Path, default=REPO_ROOT / "data/corpus/zhwiki-parquet")
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "data/corpus/zhwiki")
    parser.add_argument("--count", type=int, default=200_000, help="抽取的条目数")
    parser.add_argument("--min-chars", type=int, default=200, help="正文少于这么多字的条目会被跳过")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--keep-traditional", action="store_true", help="不做繁转简（不需要安装 opencc）")
    args = parser.parse_args()

    try:
        import pyarrow.compute as pc
        import pyarrow.parquet as pq
    except ImportError:
        raise SystemExit("pyarrow is required: python3 -m venv .venv && .venv/bin/pip install pyarrow opencc")

    convert = None
    if not args.keep_traditional:
        try:
            import opencc
        except ImportError:
            raise SystemExit("opencc is required (or pass --keep-traditional): .venv/bin/pip install opencc")
        convert = opencc.OpenCC("t2s").convert

    files = sorted(args.parquet_dir.glob("*.parquet"))
    if not files:
        raise SystemExit(f"no parquet files under {args.parquet_dir} (see the download commands in this script's header)")
    started = time.time()

    # ---- 第一遍：只看正文长度，找出所有“够长”的条目的位置 (文件序号, row group 序号, 组内行号) ----
    # 分两遍是为了让抽样结果精确且可复现：先知道候选总数，才能用固定种子从中恰好抽出 N 篇
    eligible: list[tuple[int, int, int]] = []
    total = 0
    for file_index, path in enumerate(files):
        parquet = pq.ParquetFile(path)
        for group in range(parquet.num_row_groups):
            lengths = pc.utf8_length(parquet.read_row_group(group, columns=["text"]).column("text")).to_pylist()
            total += len(lengths)
            eligible.extend((file_index, group, row) for row, length in enumerate(lengths) if length and length >= args.min_chars)
    print(f"scanned {total} articles in {len(files)} file(s); {len(eligible)} have >= {args.min_chars} chars "
          f"({len(eligible) / total:.1%})")

    count = min(args.count, len(eligible))
    chosen = sorted(random.Random(args.seed).sample(eligible, count))  # 排序后按原始顺序输出，第二遍可以顺序读

    # ---- 第二遍：只读被抽中的 row group，转换并写出 ----
    args.out.mkdir(parents=True, exist_ok=True)
    target = args.out / "zhwiki-000000000000.jsonl"  # 命名与 BigQuery 导出的分片一致
    written = converted = out_chars = 0
    position = 0
    with target.open("w", encoding="utf-8", newline="\n") as output:
        for file_index, path in enumerate(files):
            parquet = pq.ParquetFile(path)
            for group in range(parquet.num_row_groups):
                rows = []
                while position < count and chosen[position][:2] == (file_index, group):
                    rows.append(chosen[position][2])
                    position += 1
                if not rows:
                    continue
                table = parquet.read_row_group(group, columns=["url", "title", "text"]).take(rows)
                for url, title, text in zip(*(table.column(name).to_pylist() for name in ("url", "title", "text"))):
                    if convert is not None:
                        simplified_title, simplified_text = convert(title), convert(text)
                        converted += simplified_text != text or simplified_title != title
                        title, text = simplified_title, simplified_text
                    output.write(json.dumps({"url": url, "title": title, "content": text}, ensure_ascii=False))
                    output.write("\n")
                    written += 1
                    out_chars += len(text)
                if written and written % 20000 < len(rows):
                    print(f"  {written}/{count} written ...", flush=True)

    size_mb = target.stat().st_size / 2**20
    print(f"wrote {written} articles to {target} ({size_mb:.0f} MiB, avg {out_chars // max(1, written)} chars) "
          f"in {time.time() - started:.0f}s")
    if convert is not None:
        print(f"traditional -> simplified changed {converted} of them ({converted / max(1, written):.1%})")


if __name__ == "__main__":
    main()
