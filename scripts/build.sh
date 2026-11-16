#!/usr/bin/env bash
# 编译 tspi-usbmon。
#
#   在泰山派上直接编译（推荐，Ubuntu 镜像里 apt 装依赖最省事）：
#       sudo apt install -y build-essential libdrm-dev libturbojpeg0-dev pkg-config
#       ./scripts/build.sh
#
#   在 x86_64 上交叉编译（需要 aarch64 交叉编译器 + 带 libdrm/turbojpeg 的 sysroot）：
#       CROSS_COMPILE=aarch64-linux-gnu- SYSROOT=/path/to/sysroot ./scripts/build.sh
#
# SPDX-License-Identifier: MIT
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
CLEAN=0
DO_TEST=0
CROSS=${CROSS_COMPILE:-}
SYSROOT=${SYSROOT:-}

while [ $# -gt 0 ]; do
    case "$1" in
        --clean) CLEAN=1 ;;
        --test) DO_TEST=1 ;;
        --cross) CROSS=$2; shift ;;
        --sysroot) SYSROOT=$2; shift ;;
        -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

info() { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[!]\033[0m %s\n' "$*"; }

cd "$HERE"

if [ "$CLEAN" = 1 ]; then
    make clean
fi

if [ "$DO_TEST" = 1 ]; then
    info "运行主机端单元测试"
    make test
fi

if [ -z "$CROSS" ]; then
    if [ "$(uname -m)" = "aarch64" ]; then
        info "本机为 aarch64，直接本地编译"
    elif command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
        warn "检测到 aarch64-linux-gnu-gcc，但未设置 SYSROOT；若链接失败请指定 --sysroot"
        CROSS=aarch64-linux-gnu-
    else
        cat >&2 <<EOF
[x] 没有找到 aarch64 交叉编译器。
    最省事的方式是在泰山派上直接编译：
        ssh <板子>
        sudo apt install -y build-essential libdrm-dev libturbojpeg0-dev pkg-config
        git clone/push 本项目后执行 ./scripts/build.sh && sudo make install
    或者安装交叉工具链后用：CROSS_COMPILE=aarch64-linux-gnu- ./scripts/build.sh
EOF
        exit 1
    fi
fi

MAKE_ARGS=(CC="${CROSS}gcc")
if [ -n "$SYSROOT" ]; then
    MAKE_ARGS+=(SYSROOT="$SYSROOT")
fi

info "编译（CC=${CROSS}gcc）"
make -j"$(nproc)" "${MAKE_ARGS[@]}"

info "完成：$HERE/build/tspi-usbmon"
file build/tspi-usbmon 2>/dev/null || true
