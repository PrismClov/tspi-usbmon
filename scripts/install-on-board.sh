#!/usr/bin/env bash
# 在泰山派板子上安装 / 升级 tspi-usbmon（需要 root）。
#
#   sudo ./scripts/install-on-board.sh [--no-start]
#
# SPDX-License-Identifier: MIT
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
START=1

for arg in "$@"; do
    case "$arg" in
        --no-start) START=0 ;;
        -h|--help) sed -n '2,6p' "$0"; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

info() { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[x]\033[0m %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" = 0 ] || die "请用 root 运行（sudo $0）"
[ -x "$HERE/build/tspi-usbmon" ] || die "找不到 build/tspi-usbmon，请先 ./scripts/build.sh"

install -Dm755 "$HERE/build/tspi-usbmon" /usr/local/bin/tspi-usbmon
info "已安装 /usr/local/bin/tspi-usbmon"

if [ -f /etc/tspi-usbmon.conf ]; then
    info "保留已有配置 /etc/tspi-usbmon.conf"
else
    install -Dm644 "$HERE/deploy/tspi-usbmon.conf" /etc/tspi-usbmon.conf
    info "已安装默认配置 /etc/tspi-usbmon.conf"
fi

if [ -f "$HERE/assets/splash.jpg" ]; then
    install -Dm644 "$HERE/assets/splash.jpg" /usr/share/tspi-usbmon/splash.jpg
    info "已安装待机图 /usr/share/tspi-usbmon/splash.jpg"
fi

install -Dm644 "$HERE/deploy/tspi-usbmon.service" /etc/systemd/system/tspi-usbmon.service
systemctl daemon-reload
systemctl enable tspi-usbmon.service >/dev/null 2>&1 || true
info "已安装并启用 systemd 服务"

if systemctl is-active --quiet adbd 2>/dev/null; then
    printf '\033[1;33m[!]\033[0m 检测到 adbd 正在运行，它会占用 UDC 导致副屏 gadget 绑定失败。\n'
    printf '    建议：sudo systemctl disable --now adbd\n'
    printf '    或者在 /etc/tspi-usbmon.conf 里设置 takeover_udc = 1（会断开 adb）\n'
fi

if [ "$START" = 1 ]; then
    if systemctl is-active --quiet display-manager 2>/dev/null; then
        printf '\033[1;33m[!]\033[0m 检测到图形登录界面，它会抢占 DRM master。\n'
        printf '    需要副屏独占面板时执行：sudo systemctl disable --now gdm3（或 lightdm）\n'
    fi
    systemctl restart tspi-usbmon.service
    sleep 1
    systemctl --no-pager --lines=15 status tspi-usbmon.service || true
fi

cat <<'EOF'

常用命令：
  systemctl status tspi-usbmon
  journalctl -u tspi-usbmon -f
  cat /sys/class/drm/card0-DSI-1/status      # 面板是否 connected
  ls /sys/class/udc                          # 设备控制器
  cat /sys/kernel/config/usb_gadget/tspi_udisp/UDC
EOF
