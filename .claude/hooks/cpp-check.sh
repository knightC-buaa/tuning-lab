#!/usr/bin/env bash
# PostToolUse 钩子：对编辑过的 C/C++ 文件做两项检查：
#   1) clang-format --dry-run --Werror（只报告，不自动改文件）
#   2) clang-tidy，只检查本次改动的行
# 有诊断时写到 stderr 并以退出码 2 返回，Claude Code 会将其反馈给模型。
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

cd "$root" || exit 0
messages=""

# 1) 格式化门禁：只报告，不自动修改文件。
if command -v clang-format >/dev/null 2>&1; then
  if ! fmt_out="$(clang-format --style=file --dry-run --Werror "$file" 2>&1)"; then
    messages="${messages}clang-format 要求格式化本文件（运行 clang-format -i \"$rel\" 修复）：
${fmt_out}
"
  fi
fi

# 2) 静态检查：只检查 compile DB 中的源文件与本次改动的行。
build_dir="$root/build"
[ -f "$build_dir/compile_commands.json" ] || build_dir="$root/build-strict"
[ -f "$build_dir/compile_commands.json" ] || build_dir="$root/build-sanitize"
tidydiff="$(command -v clang-tidy-diff-21.py || command -v clang-tidy-diff.py || true)"

if [ -f "$build_dir/compile_commands.json" ] && [ -f "$root/.clang-tidy" ] &&
  command -v clang-tidy >/dev/null 2>&1 && [ -n "$tidydiff" ] &&
  jq -e --arg f "$file" 'any(.[]; .file == $f)' "$build_dir/compile_commands.json" >/dev/null 2>&1; then
  if git ls-files --error-unmatch "$rel" >/dev/null 2>&1; then
    diff_text="$(git diff -U0 -- "$rel")"
    if [ -n "$diff_text" ]; then
      out="$(printf '%s\n' "$diff_text" |
        "$tidydiff" -p1 -path "$build_dir" -config-file "$root/.clang-tidy" -timeout 30 2>&1)" || true
      findings="$(printf '%s\n' "$out" | grep -E '(warning|error):' || true)"
      if [ -n "$findings" ]; then
        messages="${messages}clang-tidy 发现与本次改动相关的问题，请修复（不要用 NOLINT 掩盖）：
${findings}
"
      fi
    fi
  else
    out="$(clang-tidy -p "$build_dir" --config-file="$root/.clang-tidy" --quiet "$file" 2>&1)" || true
    findings="$(printf '%s\n' "$out" | grep -E '(warning|error):' || true)"
    if [ -n "$findings" ]; then
      messages="${messages}clang-tidy 发现与本次改动相关的问题，请修复（不要用 NOLINT 掩盖）：
${findings}
"
    fi
  fi
fi

if [ -n "$messages" ]; then
  printf '%s' "$messages" >&2
  exit 2
fi
exit 0
