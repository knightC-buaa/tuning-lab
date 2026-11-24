#!/usr/bin/env bash
# 测试夹具：--verify 输出与 baseline 不一致的 checksum。
echo "fake verify bad optimized"
printf '%s\n' '{"exp":"fake","variant":"optimized","checksum":"0x2b","verify":true}'
exit 0
