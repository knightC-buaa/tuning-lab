#!/usr/bin/env bash
# 测试夹具：--verify 输出与 baseline 一致的 checksum。
echo "fake verify optimized"
printf '%s\n' '{"exp":"fake","variant":"optimized","checksum":"0x2a","verify":true}'
exit 0
