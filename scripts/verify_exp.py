#!/usr/bin/env python3
"""比较两个实验二进制的 --verify checksum。

用法: verify_exp.py <baseline_bin> <optimized_bin>

退出码：0 一致；1 错误或不一致；2 环境不满足（SKIP）。
"""

import json
import subprocess
import sys


def run_verify(path):
    try:
        return subprocess.run(
            [path, "--verify"],
            capture_output=True,
            text=True,
        )
    except OSError as exc:
        print(f"错误: 无法执行 {path}: {exc}", file=sys.stderr)
        return None


def last_json_line(stdout):
    lines = [line for line in stdout.splitlines() if line.strip()]
    if not lines:
        return None, "stdout 为空"
    try:
        obj = json.loads(lines[-1])
    except json.JSONDecodeError as exc:
        return None, f"最后一行不是合法 JSON: {exc}"
    if not isinstance(obj, dict):
        return None, "最后一行不是 JSON 对象"
    return obj, None


def main(argv):
    if len(argv) != 3:
        print("用法: verify_exp.py <baseline_bin> <optimized_bin>", file=sys.stderr)
        return 1

    results = []
    for path in argv[1:]:
        proc = run_verify(path)
        if proc is None:
            return 1

        obj, parse_error = last_json_line(proc.stdout)
        if proc.returncode == 2:
            reason = obj.get("skip") if obj else None
            detail = f"（{reason}）" if reason else ""
            print(f"SKIP: {path} 环境不满足{detail}")
            return 2
        if proc.returncode != 0:
            print(f"错误: {path} 退出码 {proc.returncode}", file=sys.stderr)
            if proc.stderr.strip():
                print(proc.stderr.rstrip(), file=sys.stderr)
            return 1
        if parse_error is not None:
            print(f"错误: {path} {parse_error}", file=sys.stderr)
            return 1

        checksum = obj.get("checksum")
        if checksum is None:
            print(f"错误: {path} 输出缺少 checksum", file=sys.stderr)
            return 1
        results.append((path, str(checksum)))

    (base_path, base_checksum), (opt_path, opt_checksum) = results
    if base_checksum != opt_checksum:
        print(
            f"错误: checksum 不一致: {base_path}={base_checksum}, "
            f"{opt_path}={opt_checksum}",
            file=sys.stderr,
        )
        return 1

    print(f"verify 通过: checksum={base_checksum}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
