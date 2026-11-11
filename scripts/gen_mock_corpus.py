#!/usr/bin/env python3
"""生成“BigQuery 导出格式”的模拟新闻语料，用于在没有真实数据时跑通/压测整条链路。

输出格式与 BigQuery 导出到 GCS 的结果一致，将来可以直接用真实导出文件替换：

  * 文件格式：换行分隔 JSON（NDJSON），一行一条记录，UTF-8
  * 文件命名：<prefix>-000000000000.jsonl、<prefix>-000000000001.jsonl ...
              （BigQuery 用通配 URI 导出时，会把 * 替换成 12 位补零的分片序号）
  * 字段：    id / url / title / content / source / category / publish_time
              搜索引擎只读取 url、title、content 三个字段，其余字段会被忽略

对应的真实导出语句示例（把表名和字段换成你自己的）：

  EXPORT DATA OPTIONS (
    uri = 'gs://my-bucket/news/news-*.jsonl',
    format = 'JSON',
    overwrite = true
  ) AS
  SELECT id, url, title, body AS content, source, category, publish_time
  FROM `my_project.my_dataset.news`;

  gsutil -m cp 'gs://my-bucket/news/news-*.jsonl' data/corpus/gcp/

用法：
  python scripts/gen_mock_corpus.py                      # 默认 20000 篇
  python scripts/gen_mock_corpus.py --docs 1000000       # 百万级压测
"""
from __future__ import annotations

import argparse
import json
import random
import time
from collections import deque
from datetime import datetime, timedelta, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

CATEGORIES = ["politics", "finance", "tech", "sports", "world", "health", "education", "culture", "auto", "travel"]
SOURCES = ["新华社", "人民网", "央视网", "中新网", "澎湃新闻", "财新网", "界面新闻", "Reuters", "AP", "Bloomberg"]
EDITORS = ["张伟", "王芳", "李娜", "刘洋", "陈静", "杨帆", "赵磊", "黄敏", "周杰", "吴倩"]
PUNCTUATION = ["，", "，", "，", "。", "。", "；", "、"]

ENGLISH_COMMON = (
    "market government company report people year technology data research system service policy growth "
    "energy climate health school city country world business industry project development team network "
    "security investment trade product customer software cloud search engine index query latency cache "
    "memory storage cluster database server protocol release update analysis economy bank price supply "
    "demand election minister president court law science student university hospital vaccine travel"
).split()
ENGLISH_SYLLABLES = "ka lo mi ren ta vo zu shi be dor fen gal hu jin qua rix sol tem ul wy xen yor".split()


def load_vocabulary(dict_path: Path) -> tuple[list[str], list[str]]:
    """从 jieba 词典里取词：既保证都是“真词”（分词器能原样切回来），又有足够大的词汇量。

    返回 (高频通用词, 中频主题词)。词汇量大、文档之间重合度低，才接近真实新闻的统计特性；
    如果用几百个词拼文章，任意两篇的 SimHash 都会很接近，去重会把大半语料误杀。
    """
    words: list[tuple[int, str]] = []
    with dict_path.open(encoding="utf-8") as file:
        for line in file:
            parts = line.split()
            if len(parts) < 2 or not parts[1].isdigit():
                continue
            word, frequency = parts[0], int(parts[1])
            if 2 <= len(word) <= 4 and all("一" <= ch <= "鿿" for ch in word):
                words.append((frequency, word))
    words.sort(reverse=True)
    common = [word for _, word in words[:3000]]
    topical = [word for _, word in words[3000:63000]]
    return common, topical


class MockNewsGenerator:
    def __init__(self, seed: int, common: list[str], topical: list[str], topic_count: int = 3000):
        self.random = random.Random(seed)
        self.common = common
        self.topical = topical
        # 每个“主题”是 200 个主题词的集合；同主题的文章共享一部分词汇
        self.topics = [self.random.sample(topical, 200) for _ in range(topic_count)]
        self.start_time = datetime(2025, 1, 1, tzinfo=timezone.utc)

    def _english_name(self) -> str:
        return "".join(self.random.choices(ENGLISH_SYLLABLES, k=self.random.randint(2, 4)))

    def _body(self, english: bool) -> tuple[str, list[str]]:
        rnd = self.random
        topic = rnd.choice(self.topics)
        if english:
            own = [self._english_name() for _ in range(25)]
            pool_topic = [self._english_name() for _ in range(10)] + rnd.sample(ENGLISH_COMMON, 30)
            pool_common = ENGLISH_COMMON
        else:
            own = rnd.sample(self.topical, 25)  # 这篇文章自己的“专有词”，会反复出现
            pool_topic = topic
            pool_common = self.common

        length = rnd.randint(120, 360)
        words = (
            rnd.choices(own, k=int(length * 0.40))
            + rnd.choices(pool_topic, k=int(length * 0.35))
            + rnd.choices(pool_common, k=int(length * 0.25))
        )
        rnd.shuffle(words)

        # 每 4~9 个词（英文 8~16 个）组成一个分句，分句之间用标点隔开
        clauses: list[str] = []
        cursor = 0
        while cursor < len(words):
            size = rnd.randint(8, 16) if english else rnd.randint(4, 9)
            clauses.append((" " if english else "").join(words[cursor:cursor + size]))
            cursor += size
        if english:
            pieces = [". ".join(clauses)]
        else:
            pieces = [clause + rnd.choice(PUNCTUATION) for clause in clauses[:-1]] + clauses[-1:]
        if rnd.random() < 0.3:  # 掺一点数字，模拟日期/金额
            pieces.append(f" {rnd.randint(1, 9999)}")
        return "".join(pieces).strip() + ("." if english else "。"), own

    def document(self, doc_index: int) -> dict:
        rnd = self.random
        english = rnd.random() < 0.10
        content, own = self._body(english)
        title_words = rnd.sample(own, 3)
        title = " ".join(title_words).title() if english else "".join(title_words)
        category = rnd.choice(CATEGORIES)
        published = self.start_time + timedelta(seconds=rnd.randint(0, 600 * 24 * 3600))
        return {
            "id": f"mock-{doc_index:010d}",
            "url": f"https://news.example.com/{category}/{published:%Y/%m%d}/{doc_index:010d}.html",
            "title": title,
            "content": content,
            "source": rnd.choice(SOURCES),
            "category": category,
            # BigQuery 导出 TIMESTAMP 列时就是这种文本格式
            "publish_time": published.strftime("%Y-%m-%d %H:%M:%S UTC"),
        }

    def repost(self, original: dict, doc_index: int) -> dict:
        """模拟转载：换一个 url/来源，正文原样照搬或只在末尾加一句编辑署名。"""
        rnd = self.random
        copy = dict(original)
        copy["id"] = f"mock-{doc_index:010d}"
        copy["url"] = original["url"].replace("news.example.com", "mirror.example.org")
        copy["source"] = rnd.choice(SOURCES)
        if rnd.random() < 0.5:
            copy["content"] = original["content"] + f"（责任编辑：{rnd.choice(EDITORS)}）"
        return copy


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--docs", type=int, default=20000, help="文档总数（默认 20000）")
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "data" / "corpus" / "gcp", help="输出目录")
    parser.add_argument("--prefix", default="news", help="文件名前缀")
    parser.add_argument("--shard-size", type=int, default=100000, help="每个分片文件的记录数")
    parser.add_argument("--dup-ratio", type=float, default=0.05, help="转载（重复/近似重复）文档占比")
    parser.add_argument("--seed", type=int, default=20260101, help="随机种子（相同种子生成完全相同的语料）")
    parser.add_argument("--jieba-dict", type=Path, default=REPO_ROOT / "data" / "jieba" / "jieba.dict.utf8")
    args = parser.parse_args()

    started = time.time()
    common, topical = load_vocabulary(args.jieba_dict)
    generator = MockNewsGenerator(args.seed, common, topical)

    args.out.mkdir(parents=True, exist_ok=True)
    for stale in args.out.glob(f"{args.prefix}-*.jsonl"):  # 清掉上次生成的分片，避免新旧混在一起
        stale.unlink()

    recent: deque[dict] = deque(maxlen=2000)
    reposts = 0
    total_bytes = 0
    shard_file = None
    for doc_index in range(args.docs):
        if doc_index % args.shard_size == 0:
            if shard_file:
                shard_file.close()
            shard_path = args.out / f"{args.prefix}-{doc_index // args.shard_size:012d}.jsonl"
            shard_file = shard_path.open("w", encoding="utf-8", newline="\n")

        if recent and generator.random.random() < args.dup_ratio:
            row = generator.repost(generator.random.choice(recent), doc_index)
            reposts += 1
        else:
            row = generator.document(doc_index)
            recent.append(row)

        line = json.dumps(row, ensure_ascii=False)
        total_bytes += len(line.encode("utf-8")) + 1
        shard_file.write(line + "\n")

    if shard_file:
        shard_file.close()

    shards = (args.docs + args.shard_size - 1) // args.shard_size
    print(f"generated {args.docs} docs ({reposts} reposts, {args.docs - reposts} unique) "
          f"in {shards} shard(s), {total_bytes / 1e6:.1f} MB, {time.time() - started:.1f}s")
    print(f"output: {args.out}")


if __name__ == "__main__":
    main()
