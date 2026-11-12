#!/usr/bin/env python3
"""把中文维基百科的 parquet 转成 build_index 能读的 NDJSON（与 BigQuery 导出格式一致）。

数据源：Hugging Face 数据集 wikimedia/wikipedia 的中文子集 20231101.zh（CC BY-SA 3.0 / GFDL），
6 个 parquet 文件共 1.6 GiB、138 万篇，字段为 id / url / title / text（text 已去掉维基标记）。

  mkdir -p data/corpus/zhwiki-parquet && cd data/corpus/zhwiki-parquet
  for i in 0 1 2 3 4 5; do
    curl -fLO "https://huggingface.co/datasets/wikimedia/wikipedia/resolve/main/20231101.zh/train-0000$i-of-00006.parquet"
  done

  python3 -m venv .venv && .venv/bin/pip install pyarrow opencc     # 依赖装在虚拟环境里
  .venv/bin/python scripts/import_zhwiki.py                         # 全部约 79 万篇，写入 data/corpus/zhwiki/
  .venv/bin/python scripts/import_zhwiki.py --limit 20000           # 小规模测试：只取前 2 万篇

做两件事：
  1. 跳过正文过短的条目（默认少于 200 字）：消歧义页、重定向残留、只有一句话的小条目对检索没有价值。
     138 万篇里有 79 万篇（57%）达到这个长度，全部输出，不再抽样——抽样会漏掉“人工智能”“红楼梦”这样的主条目；
  2. 繁体转简体。中文维基是繁简混排（抽样统计约 61% 的条目以繁体书写），而 cppjieba 的词典是简体：
     不转换的话，繁体条目会被切成单字，用简体查询也搜不到它们。
     转换分两步：先用 OpenCC 的 t2s 逐字繁转简，再用下面 REGION_TERMS 这张小表把台湾用词换成大陆用词
     （人工智慧 → 人工智能、软体 → 软件），否则同一个概念会在索引里分裂成两个词。

     为什么不直接用 OpenCC 自带的 tw2sp（转字 + 地区用词）？抽样对比发现它的用词表是按“台湾的 IT 文档”设计的，
     用在百科全文上误转很多：车站启用 → 车站激活、理查德 → 理乍得、参考资料 → 参考数据、核心区 → 内核区、
     电子音乐 → 电辅音乐、应用程序 → 应用进程。中文维基还有约四成条目本来就是大陆用词，对它们做“台湾 → 大陆”
     的替换只有副作用。所以这里只保留一份逐条核对过上下文的、不会与大陆常用词撞车的短表。

输出每行一个 JSON 对象，只有 url / title / content 三个字段；每 10 万篇一个分片，
文件名与 BigQuery 导出的分片一致（zhwiki-000000000000.jsonl、zhwiki-000000000001.jsonl ...）。
同样的输入和参数，输出逐字节相同。
"""
from __future__ import annotations

import argparse
import json
import re
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# 台湾用词 → 大陆用词（在 t2s 之后应用，所以左边写的是“简体字形的台湾用词”）。
# 收录标准：左边的词在大陆的书面语里不是另一个意思的常用词。因此没有收录
# 资料（≠数据）、档案（≠文件）、数位（“数位专家”）、位元（“单位元”）、品质、资讯、使用者 等。
# 两处用正则的前后断言避开已知的撞车：方程式、软体动物。
REGION_TERMS = {
    "人工智慧": "人工智能", "智慧型手机": "智能手机",
    "资料库": "数据库", "资料结构": "数据结构", "资料夹": "文件夹",
    "程式码": "代码", "原始码": "源代码", r"(?<!方)程式": "程序", "函式": "函数", "执行绪": "线程", "演算法": "算法",
    r"软体(?!动物)": "软件", "硬体": "硬件", "韧体": "固件", "作业系统": "操作系统",
    "网际网路": "互联网", "网路": "网络", "外部连结": "外部链接", "超连结": "超链接", "部落格": "博客",
    "伺服器": "服务器", "记忆体": "内存", "位元组": "字节", "汇流排": "总线",
    "硬碟": "硬盘", "光碟": "光盘", "磁碟": "磁盘", "随身碟": "U盘",
    "积体电路": "集成电路", "电晶体": "晶体管", "晶片": "芯片",
    "滑鼠": "鼠标", "萤幕": "屏幕", "印表机": "打印机", "解析度": "分辨率",
    "介面": "界面", "视窗": "窗口", "选单": "菜单", "登入": "登录", "帐号": "账号",
    "行动电话": "移动电话", "简讯": "短信",
    "奈米": "纳米", "雷射": "激光", "太空梭": "航天飞机", "计程车": "出租车",
    "义大利": "意大利", "纽西兰": "新西兰", "寮国": "老挝", "沙乌地阿拉伯": "沙特阿拉伯", "奈及利亚": "尼日利亚",
}


def build_region_converter():
    """把 REGION_TERMS 编译成一个正则，一趟扫描完成全部替换。长词排在前面：“程式码”要先于“程式”匹配。"""
    patterns = sorted(REGION_TERMS, key=lambda pattern: -len(re.sub(r"\(\?.*?\)", "", pattern)))
    replacement_of = {re.sub(r"\(\?.*?\)", "", pattern): REGION_TERMS[pattern] for pattern in patterns}
    regex = re.compile("|".join(patterns))
    return lambda text: regex.sub(lambda match: replacement_of[match.group(0)], text)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--parquet-dir", type=Path, default=REPO_ROOT / "data/corpus/zhwiki-parquet")
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "data/corpus/zhwiki")
    parser.add_argument("--min-chars", type=int, default=200, help="正文少于这么多字的条目会被跳过")
    parser.add_argument("--limit", type=int, default=0, help="最多输出多少篇（0 = 不限），用于小规模测试")
    parser.add_argument("--shard-size", type=int, default=100_000, help="每个输出分片的条目数")
    parser.add_argument("--opencc", default="t2s", help="OpenCC 配置，默认 t2s（只转字）")
    parser.add_argument("--no-region-terms", action="store_true", help="不做 REGION_TERMS 的地区用词替换")
    parser.add_argument("--keep-traditional", action="store_true", help="不做繁转简（不需要安装 opencc）")
    args = parser.parse_args()

    try:
        import pyarrow.parquet as pq
    except ImportError:
        raise SystemExit("pyarrow is required: python3 -m venv .venv && .venv/bin/pip install pyarrow opencc")

    convert = None
    if not args.keep_traditional:
        try:
            import opencc
        except ImportError:
            raise SystemExit("opencc is required (or pass --keep-traditional): .venv/bin/pip install opencc")
        to_simplified = opencc.OpenCC(args.opencc).convert
        if args.no_region_terms:
            convert = to_simplified
        else:
            to_mainland_terms = build_region_converter()
            convert = lambda text: to_mainland_terms(to_simplified(text))  # noqa: E731

    files = sorted(args.parquet_dir.glob("*.parquet"))
    if not files:
        raise SystemExit(f"no parquet files under {args.parquet_dir} (see the download commands in this script's header)")
    started = time.time()

    # 上一次运行留下的分片要先删掉：这次的分片数可能更少，残留的旧文件会被 build_index 一起读进去
    args.out.mkdir(parents=True, exist_ok=True)
    for stale in args.out.glob("zhwiki-*.jsonl"):
        stale.unlink()

    total = skipped = written = converted = out_chars = 0
    output = None
    shard_paths: list[Path] = []
    done = False
    for path in files:
        parquet = pq.ParquetFile(path)
        for group in range(parquet.num_row_groups):  # 一次只读一个 row group，内存占用与语料总量无关
            table = parquet.read_row_group(group, columns=["url", "title", "text"])
            for url, title, text in zip(*(table.column(name).to_pylist() for name in ("url", "title", "text"))):
                total += 1
                if not text or len(text) < args.min_chars:
                    skipped += 1
                    continue
                if convert is not None:
                    simplified_title, simplified_text = convert(title), convert(text)
                    converted += simplified_text != text or simplified_title != title
                    title, text = simplified_title, simplified_text
                if written % args.shard_size == 0:
                    if output is not None:
                        output.close()
                    shard_paths.append(args.out / f"zhwiki-{len(shard_paths):012d}.jsonl")
                    output = shard_paths[-1].open("w", encoding="utf-8", newline="\n")
                    print(f"  {written} written, starting {shard_paths[-1].name} ...", flush=True)
                output.write(json.dumps({"url": url, "title": title, "content": text}, ensure_ascii=False))
                output.write("\n")
                written += 1
                out_chars += len(text)
                if args.limit and written >= args.limit:
                    done = True
                    break
            if done:
                break
        if done:
            break
    if output is not None:
        output.close()

    size_mb = sum(path.stat().st_size for path in shard_paths) / 2**20
    print(f"scanned {total} articles in {len(files)} file(s); skipped {skipped} shorter than {args.min_chars} chars")
    print(f"wrote {written} articles to {args.out} in {len(shard_paths)} shard(s) "
          f"({size_mb:.0f} MiB, avg {out_chars // max(1, written)} chars) in {time.time() - started:.0f}s")
    if convert is not None:
        print(f"traditional -> simplified ({args.opencc}{'' if args.no_region_terms else ' + region terms'}) changed {converted} of them ({converted / max(1, written):.1%})")


if __name__ == "__main__":
    main()
