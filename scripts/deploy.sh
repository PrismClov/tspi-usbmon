#!/usr/bin/env bash
# 把已编译好的程序推送到泰山派并安装。
#
#   BOARD=neons@192.168.1.50 ./scripts/deploy.sh
#   BOARD=tspi ./scripts/deploy.sh --no-start
#
# 板子需要能 ssh 且 sudo 免密（或执行时输入密码）。
# SPDX-License-Identifier: MIT
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BOARD=${BOARD:-}
NO_START=""

while [ $# -gt 0 ]; do
    case "$1" in
        --no-start) NO_START="--no-start" ;;
        -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

info() { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[x]\033[0m %s\n' "$*" >&2; exit 1; }

[ -n "$BOARD" ] || die "请设置 BOARD=user@host（例如 BOARD=neons@192.168.1.50）"
[ -x "$HERE/build/tspi-usbmon" ] || die "请先编译：./scripts/build.sh"

info "打包并推送到 $BOARD"
tar -C "$HERE" -czf - build/tspi-usbmon assets/splash.jpg deploy scripts/install-on-board.sh \
    | ssh "$BOARD" 'rm -rf /tmp/tspi-usbmon-deploy && mkdir -p /tmp/tspi-usbmon-deploy && tar -C /tmp/tspi-usbmon-deploy -xzf -'

info "在板子上安装"
ssh -t "$BOARD" "sudo /tmp/tspi-usbmon-deploy/scripts/install-on-board.sh $NO_START"

info "完成。查看日志：ssh $BOARD 'journalctl -u tspi-usbmon -f'"
