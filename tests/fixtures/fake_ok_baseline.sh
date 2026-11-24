#!/usr/bin/env bash
# 测试夹具：--verify 输出与 optimized 一致的 checksum。
echo "fake verify baseline"
printf '%s\n' '{"exp":"fake","variant":"baseline","checksum":"0x2a","verify":true}'
exit 0
