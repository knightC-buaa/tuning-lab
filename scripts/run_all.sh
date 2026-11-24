#!/usr/bin/env bash
# 运行全部实验：构建 → 正确性闸门 → 多轮测量 → 汇总报告。
#
# 环境变量：
#   RUNS  测量轮数，默认 3（每轮反转 baseline/optimized 顺序）。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

RUNS="${RUNS:-3}"
if ! [[ "$RUNS" =~ ^[1-9][0-9]*$ ]]; then
  echo "错误: RUNS 必须是正整数，收到 '$RUNS'" >&2
  exit 1
fi

# 提升文件描述符上限；失败不致命。
HARD_FD="$(ulimit -Hn 2>/dev/null || true)"
if [[ "$HARD_FD" == "unlimited" ]]; then
  ulimit -n unlimited 2>/dev/null || true
elif [[ -n "$HARD_FD" ]]; then
  ulimit -n "$HARD_FD" 2>/dev/null || true
fi

echo "== 配置并构建 Release =="
cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build" -j"$(nproc)"

echo "== 正确性闸门：CTest =="
ctest --test-dir "$ROOT/build" --output-on-failure

TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
OUT_DIR="$ROOT/results/$TIMESTAMP"
mkdir -p "$OUT_DIR"
RAW="$OUT_DIR/raw.jsonl"
: > "$RAW"

EXP_INDEX="$ROOT/build/exp_index.txt"
if [[ ! -f "$EXP_INDEX" ]]; then
  echo "错误: 找不到 $EXP_INDEX，请先配置构建" >&2
  exit 1
fi
mapfile -t EXPERIMENTS < <(grep -v '^[[:space:]]*$' "$EXP_INDEX" || true)
EXP_COUNT="${#EXPERIMENTS[@]}"

echo "== 测量：$RUNS 轮，$EXP_COUNT 个实验 =="
for ((round = 1; round <= RUNS; ++round)); do
  if ((round % 2 == 1)); then
    VARIANTS=(baseline optimized)
  else
    VARIANTS=(optimized baseline)
  fi

  for exp in "${EXPERIMENTS[@]+"${EXPERIMENTS[@]}"}"; do
    slug="${exp//./_}"
    for variant in "${VARIANTS[@]}"; do
      binary="$ROOT/build/exp/${slug}_${variant}"
      if [[ ! -x "$binary" ]]; then
        echo "错误: 找不到可执行文件 $binary" >&2
        exit 1
      fi

      rc=0
      "$binary" --reps 5 --cpu 2 >> "$RAW" || rc=$?
      if ((rc == 2)); then
        echo "第 $round 轮 $exp $variant: SKIP"
      elif ((rc != 0)); then
        echo "错误: 第 $round 轮 $exp $variant 退出码 $rc" >&2
        exit 1
      fi
    done
  done
done

echo "== 生成汇总报告 =="
python3 "$ROOT/scripts/report.py" "$RAW" > "$OUT_DIR/summary.md"
echo "原始数据: $RAW"
echo "汇总报告: $OUT_DIR/summary.md"
