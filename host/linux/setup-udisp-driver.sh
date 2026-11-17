#!/bin/sh
# 在 Linux 主机上编译并加载 udisp_drm 内核模块（需要 root）。
#
#   sudo ./setup-udisp-driver.sh
#
# 加载成功后，桌面会多出一块 USB 显示器（设备为 303a:2986）。
# SPDX-License-Identifier: MIT（本脚本）；udisp_drm/ 为 GPL-2.0 上游代码
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
KREL=$(uname -r)

[ "$(id -u)" = 0 ] || { echo "请用 root 运行：sudo $0" >&2; exit 1; }

if [ ! -d "/lib/modules/$KREL/build" ]; then
    echo "==> 安装内核头文件"
    apt-get update
    apt-get install -y "linux-headers-$KREL"
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v make >/dev/null 2>&1; then
    echo "==> 安装编译工具"
    apt-get update
    apt-get install -y build-essential
fi

# Ubuntu 的内核头文件把 CC 写死成构建内核时用的那个 gcc（例如 gcc-12），
# 和 build-essential 装的 gcc-11 不一样，必须按内核的要求补齐。
KCC=$(sed -n 's/^CC[[:space:]]*=[[:space:]]*\$(CROSS_COMPILE)//p' \
      "/lib/modules/$KREL/build/Makefile" 2>/dev/null | head -n1 | tr -d ' ')
[ -n "$KCC" ] || KCC=gcc
if ! command -v "$KCC" >/dev/null 2>&1; then
    echo "==> 内核使用 $KCC，安装对应编译器"
    apt-get update
    apt-get install -y "$KCC"
fi
echo "==> 使用编译器 $KCC"

echo "==> 编译 udisp_drm"
make -C "$HERE/udisp_drm" clean >/dev/null 2>&1 || true
make -C "$HERE/udisp_drm" CC="$KCC"

echo "==> 安装模块与 modprobe 配置"
make -C "$HERE/udisp_drm" install
install -m644 "$HERE/udisp_drm.conf" /etc/modprobe.d/udisp_drm.conf

echo "==> 加载"
modprobe -r udisp_drm 2>/dev/null || true
modprobe udisp_drm || true
sleep 2

echo
echo "==> 状态"
lsmod | grep udisp || echo "!! 模块没加载成功，看 dmesg"
dmesg | grep -i udisp | tail -6 || true
echo
echo "==> DRM 设备"
ls /dev/dri/
for c in /sys/class/drm/card*-*; do
    [ -d "$c" ] || continue
    echo "  $c: $(cat "$c/status" 2>/dev/null)"
done
echo
echo "如果桌面没出现新显示器：journalctl -k | tail -20 看驱动日志，"
echo "或者试 connector_type=hdmi（编辑 /etc/modprobe.d/udisp_drm.conf 后重新 modprobe）。"
