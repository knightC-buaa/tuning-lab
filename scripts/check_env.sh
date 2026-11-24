#!/usr/bin/env bash
# 打印测量环境信息。只读：不调用 sudo，不修改任何系统设置。
set -u

echo "== 测量环境 =="

cpu_model="$(awk -F': ' '/^model name[[:space:]]*:/ {print $2; exit}' /proc/cpuinfo 2>/dev/null || true)"
printf 'CPU 型号: %s\n' "${cpu_model:-未知}"

physical="未知"
if command -v lscpu >/dev/null 2>&1; then
  physical="$(lscpu -p=Core,Socket 2>/dev/null | grep -v '^#' | sort -u | wc -l | tr -d ' ')"
  logical="$(lscpu -p=CPU 2>/dev/null | grep -v '^#' | wc -l | tr -d ' ')"
else
  logical="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 未知)"
  physical="$(awk -F': ' '/^physical id/ {p=$2} /^core id/ {print p ":" $2}' /proc/cpuinfo 2>/dev/null | sort -u | wc -l | tr -d ' ')"
fi
printf '物理核: %s\n' "$physical"
printf '逻辑 CPU: %s\n' "${logical:-未知}"

governor="不可用"
governor_file="/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
if [[ -r "$governor_file" ]]; then
  governor="$(cat "$governor_file")"
fi
printf 'cpufreq governor: %s\n' "$governor"
if [[ "$governor" != "performance" ]]; then
  echo "提示: governor 不是 performance，可手动执行（需 root；本脚本不会执行）:"
  echo "  sudo cpupower frequency-set -g performance"
fi

paranoid="不可用"
if [[ -r /proc/sys/kernel/perf_event_paranoid ]]; then
  paranoid="$(cat /proc/sys/kernel/perf_event_paranoid)"
fi
printf 'perf_event_paranoid: %s\n' "$paranoid"

thp="不可用"
if [[ -r /sys/kernel/mm/transparent_hugepage/enabled ]]; then
  thp="$(cat /sys/kernel/mm/transparent_hugepage/enabled)"
fi
printf 'THP: %s\n' "$thp"

if [[ -r /proc/meminfo ]]; then
  available_ram="$(awk '/^MemAvailable:/ {printf "%.1f GiB", $2 / 1048576}' /proc/meminfo)"
else
  available_ram="未知"
fi
printf '可用内存: %s\n' "${available_ram:-未知}"

printf 'ulimit -n: %s\n' "$(ulimit -n 2>/dev/null || echo 未知)"

liburing="未找到"
for inc in /usr/include /usr/local/include; do
  if [[ -f "$inc/liburing.h" ]]; then
    liburing="$inc/liburing.h"
    break
  fi
done
if [[ "$liburing" == "未找到" ]] && command -v cc >/dev/null 2>&1; then
  if printf '#include <liburing.h>\n' | cc -E -x c - >/dev/null 2>&1; then
    liburing="可用（编译器可定位）"
  fi
fi
printf 'liburing.h: %s\n' "$liburing"
