#!/usr/bin/env python3
"""把基准 JSONL 汇总为 Markdown 报告。

用法: report.py <jsonl...>

规则见 .superpowers/sdd/plan/task-2-brief.md：
- verify 行忽略；skip 行使整个实验记为 SKIP。
- 同一实验内 checksum 不一致记为 INVALID。
- 某 variant 没有样本时记为 INVALID 并给出备注，而不是崩溃。
- 判定只使用 median_ns 与 p10/p90；几何平均只统计有效实验。
"""

import argparse
import json
import math
import statistics
import sys

GROUP_NAMES = {
    "2": "计算",
    "3": "存储",
    "4": "I/O",
    "5": "并发",
    "6": "分布式",
    "?": "其他",
}
GROUP_ORDER = ["2", "3", "4", "5", "6", "?"]
VALID_STATUSES = ("SIGNIFICANT", "INCONCLUSIVE", "REGRESSION")


def fmt_ns(ns):
    """把纳秒格式化为易读时长。"""
    if ns is None:
        return "N/A"
    if ns < 1_000:
        return f"{ns:.2f} ns"
    if ns < 1_000_000:
        return f"{ns / 1_000:.2f} µs"
    if ns < 1_000_000_000:
        return f"{ns / 1_000_000:.2f} ms"
    return f"{ns / 1_000_000_000:.2f} s"


def percentile(xs, p):
    """最近秩百分位，与 common/bench.cpp 的 percentile 保持一致。"""
    if not xs:
        return None
    ordered = sorted(xs)
    rank = math.ceil(p * len(ordered))
    idx = 0 if rank < 1 else rank - 1
    idx = min(idx, len(ordered) - 1)
    return ordered[idx]


def normalize_checksum(value):
    """返回可用于比较的 checksum：能转整数就转，否则保留字符串。"""
    if isinstance(value, int):
        return value
    text = str(value).strip()
    try:
        return int(text, 0)
    except (TypeError, ValueError):
        return text


def checksum_text(value):
    return hex(value) if isinstance(value, int) else str(value)


def escape_cell(text):
    return str(text).replace("|", "\\|").replace("\n", " ").strip()


def load(paths):
    """读取所有 JSONL，按 exp 聚合。"""
    experiments = {}
    for path in paths:
        try:
            handle = open(path, encoding="utf-8")
        except OSError as exc:
            print(f"警告: 无法读取 {path}: {exc}", file=sys.stderr)
            continue
        with handle:
            for lineno, raw in enumerate(handle, 1):
                raw = raw.strip()
                if not raw:
                    continue
                try:
                    row = json.loads(raw)
                except json.JSONDecodeError as exc:
                    print(f"警告: {path}:{lineno} 不是合法 JSON: {exc}", file=sys.stderr)
                    continue
                if not isinstance(row, dict) or "exp" not in row:
                    continue
                if row.get("verify"):
                    continue

                exp = str(row["exp"])
                entry = experiments.setdefault(
                    exp,
                    {"skip": None, "checksums": set(), "semantics": [], "variants": {}},
                )

                semantics = row.get("semantics")
                if semantics:
                    semantics = str(semantics)
                    if semantics not in entry["semantics"]:
                        entry["semantics"].append(semantics)

                skip = row.get("skip")
                if skip:
                    if entry["skip"] is None:
                        entry["skip"] = str(skip)
                    continue

                variant = row.get("variant")
                median = row.get("median_ns")
                if variant not in ("baseline", "optimized") or median is None:
                    continue
                entry["variants"].setdefault(variant, []).append(float(median))

                checksum = row.get("checksum")
                if checksum not in (None, ""):
                    entry["checksums"].add(normalize_checksum(checksum))
    return experiments


def analyze(exp, entry):
    """计算单个实验的判定。"""
    semantics = "；".join(entry["semantics"])
    notes = [semantics] if semantics else []

    result = {
        "exp": exp,
        "status": None,
        "baseline_ns": None,
        "optimized_ns": None,
        "speedup": None,
        "speedup_str": "N/A",
        "note": "",
    }

    baseline = entry["variants"].get("baseline", [])
    optimized = entry["variants"].get("optimized", [])

    if entry["skip"] is not None:
        result["status"] = "SKIP"
        result["note"] = entry["skip"]
        if semantics:
            result["note"] += f"；{semantics}"
        return result

    if baseline:
        result["baseline_ns"] = statistics.median(baseline)
    if optimized:
        result["optimized_ns"] = statistics.median(optimized)

    invalid_reason = None
    if len(entry["checksums"]) > 1:
        values = " vs ".join(sorted(checksum_text(v) for v in entry["checksums"]))
        invalid_reason = f"checksum 不一致：{values}"
    elif not baseline and not optimized:
        invalid_reason = "缺少 baseline 与 optimized 样本"
    elif not baseline:
        invalid_reason = "缺少 baseline 样本"
    elif not optimized:
        invalid_reason = "缺少 optimized 样本"

    if invalid_reason is None and baseline and optimized:
        base_med = result["baseline_ns"]
        opt_med = result["optimized_ns"]
        if base_med <= 0 or opt_med <= 0:
            invalid_reason = "耗时非正数，无法计算加速比"

    if invalid_reason is not None:
        result["status"] = "INVALID"
        notes.insert(0, invalid_reason)
        result["note"] = "；".join(notes)
        return result

    base_med = result["baseline_ns"]
    opt_med = result["optimized_ns"]
    speedup = base_med / opt_med
    base_p10 = percentile(baseline, 0.10)
    base_p90 = percentile(baseline, 0.90)
    opt_p10 = percentile(optimized, 0.10)
    opt_p90 = percentile(optimized, 0.90)

    if speedup >= 1.10 and base_p10 > opt_p90:
        status = "SIGNIFICANT"
    elif speedup <= 0.90 and opt_p10 > base_p90:
        status = "REGRESSION"
    else:
        status = "INCONCLUSIVE"

    result["status"] = status
    result["speedup"] = speedup
    prefix = "≥" if opt_med < 100_000 else ""
    result["speedup_str"] = f"{prefix}{speedup:.2f}x"
    result["note"] = "；".join(notes)
    return result


def render(experiments):
    lines = ["# 性能优化实验汇总", ""]
    groups = {}
    for exp, entry in experiments.items():
        key = exp[0] if exp and exp[0].isdigit() else "?"
        if key not in GROUP_NAMES:
            key = "?"
        groups.setdefault(key, []).append(analyze(exp, entry))

    if not groups:
        lines.append("没有实验数据。")
        lines.append("")
        return "\n".join(lines)

    for key in GROUP_ORDER:
        rows = groups.get(key)
        if not rows:
            continue
        rows.sort(key=lambda item: item["exp"])
        lines.append(f"## {key} {GROUP_NAMES[key]}" if key != "?" else "## 其他")
        lines.append("")
        lines.append("| 实验 | baseline | optimized | 加速比 | 判定 | 备注 |")
        lines.append("|---|---|---|---|---|---|")
        for row in rows:
            lines.append(
                "| {exp} | {base} | {opt} | {speed} | {status} | {note} |".format(
                    exp=escape_cell(row["exp"]),
                    base=fmt_ns(row["baseline_ns"]),
                    opt=fmt_ns(row["optimized_ns"]),
                    speed=row["speedup_str"],
                    status=row["status"],
                    note=escape_cell(row["note"]),
                )
            )
        lines.append("")

        counts = {name: 0 for name in (*VALID_STATUSES, "SKIP", "INVALID")}
        valid_speedups = []
        for row in rows:
            counts[row["status"]] += 1
            if row["status"] in VALID_STATUSES and row["speedup"] is not None:
                valid_speedups.append(row["speedup"])

        if valid_speedups:
            geo = math.exp(
                sum(math.log(speed) for speed in valid_speedups) / len(valid_speedups)
            )
            lines.append(
                f"几何平均加速比：{geo:.2f}x（{len(valid_speedups)} 个有效实验）"
            )
        else:
            lines.append("几何平均加速比：N/A（无有效实验）")
        lines.append("计数：" + " ".join(f"{k}={v}" for k, v in counts.items()))
        lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description="汇总基准 JSONL 为 Markdown 报告")
    parser.add_argument("jsonl", nargs="+", help="JSONL 文件路径（可多个）")
    args = parser.parse_args(argv)
    sys.stdout.write(render(load(args.jsonl)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
