#!/usr/bin/env bash
# 从天马屏适配仓库获取 Samsung sec_ts 触摸驱动源码。
#
#   ./scripts/fetch-touch-driver.sh <目标目录>
#
# 会优先使用本地已有的 mipi-hub 仓库（MIPI_HUB_DIR 或 ../ref/mipi-hub），
# 否则用 git 稀疏克隆 gitee 仓库（约 400MB，请耐心等待）。
#
# SPDX-License-Identifier: MIT
set -euo pipefail

DEST=${1:?用法: $0 目标目录}
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
REPO_URL=${MIPI_HUB_URL:-https://gitee.com/opengisbook/mipi-hub-official.git}
SUBDIR="TL060FVXS07/泰山派/my_sec_ts"
FILES=(Kconfig Makefile sec_ts.h sec_ts_main.c sec_ts_fn.c sec_ts_fw.c
       sec_ts_selftest.c sec_ts_selftest.h s6d6ft0_v1.10_20170918.i)

info() { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[x]\033[0m %s\n' "$*" >&2; exit 1; }

SRC=""
for cand in "${MIPI_HUB_DIR:-}" "$HERE/../ref/mipi-hub" "$HERE/third_party/mipi-hub"; do
    if [ -n "$cand" ] && [ -d "$cand/$SUBDIR" ]; then
        SRC=$cand/$SUBDIR
        break
    fi
done

if [ -z "$SRC" ]; then
    CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/tspi-usbmon/mipi-hub
    if [ ! -d "$CACHE/.git" ]; then
        info "稀疏克隆 mipi-hub 到 $CACHE（首次约 400MB）"
        git clone --depth 1 --filter=blob:none --sparse "$REPO_URL" "$CACHE"
    fi
    ( cd "$CACHE" && git sparse-checkout set "$SUBDIR" )
    SRC=$CACHE/$SUBDIR
fi

[ -d "$SRC" ] || die "找不到触摸驱动目录：$SRC"
mkdir -p "$DEST"
for f in "${FILES[@]}"; do
    [ -f "$SRC/$f" ] || die "缺少文件 $SRC/$f"
    install -m644 "$SRC/$f" "$DEST/$f"
done
info "已复制 $((${#FILES[@]})) 个文件到 $DEST"
