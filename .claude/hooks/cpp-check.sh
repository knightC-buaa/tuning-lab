#!/usr/bin/env bash
# PostToolUse 钩子：对编辑过的 C/C++ 文件，只检查本次改动的行。
# 有诊断时把内容写到 stderr 并以退出码 2 返回，Claude Code 会将其反馈给模型。
set -uo pipefail

input="$(cat)"
file="$(printf '%s' "$input" | jq -r '.tool_input.file_path // empty')"
[ -n "$file" ] || exit 0

case "$file" in
  *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.hxx) ;;
  *) exit 0 ;;
esac
[ -f "$file" ] || exit 0

root="${CLAUDE_PROJECT_DIR:-$(git -C "$(dirname "$file")" rev-parse --show-toplevel 2>/dev/null || pwd)}"
rel="${file#"$root"/}"
case "$rel" in
  common/*|exp/*|tests/*) ;;
  *) exit 0 ;;
esac

[ -f "$root/.clang-tidy" ] || exit 0
command -v clang-tidy >/dev/null 2>&1 || exit 0

tidydiff="$(command -v clang-tidy-diff-21.py || command -v clang-tidy-diff.py || true)"
[ -n "$tidydiff" ] || exit 0

build_dir="$root/build"
[ -f "$build_dir/compile_commands.json" ] || build_dir="$root/build-strict"
[ -f "$build_dir/compile_commands.json" ] || build_dir="$root/build-sanitize"
[ -f "$build_dir/compile_commands.json" ] || exit 0

# 只检查在 compile DB 中的源文件；头文件与新文件在重新配置前跳过。
jq -e --arg f "$file" 'any(.[]; .file == $f)' \
  "$build_dir/compile_commands.json" >/dev/null 2>&1 || exit 0

cd "$root" || exit 0

if git ls-files --error-unmatch "$rel" >/dev/null 2>&1; then
  diff_text="$(git diff -U0 -- "$rel")"
  [ -n "$diff_text" ] || exit 0
  out="$(printf '%s\n' "$diff_text" |
    "$tidydiff" -p1 -path "$build_dir" -config-file "$root/.clang-tidy" -timeout 30 2>&1)" || true
else
  # 新文件不在 git diff 中，整文件检查。
  out="$(clang-tidy -p "$build_dir" --config-file="$root/.clang-tidy" --quiet "$file" 2>&1)" || true
fi

findings="$(printf '%s\n' "$out" | grep -E '(warning|error):' || true)"
if [ -n "$findings" ]; then
  printf 'clang-tidy 发现与本次改动相关的问题，请修复（不要用 NOLINT 掩盖）：\n%s\n' "$findings" >&2
  exit 2
fi
exit 0
