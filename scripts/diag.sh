#!/usr/bin/env bash
# 一键收集副屏相关的运行状态，贴到 issue/群里之前先跑这个。
#
#   sudo ./scripts/diag.sh
#
# SPDX-License-Identifier: MIT
set -u

sep() { printf '\n\033[1;36m===== %s =====\033[0m\n' "$*"; }

sep "系统"
uname -a
[ -f /etc/os-release ] && grep -E 'PRETTY_NAME|VERSION=' /etc/os-release

sep "面板 / DRM"
ls /sys/class/drm/ 2>/dev/null
for c in /sys/class/drm/card*-DSI-*; do
    [ -d "$c" ] || continue
    echo "-- $c: $(cat "$c/status" 2>/dev/null)"
    echo "   modes: $(cat "$c/modes" 2>/dev/null | tr '\n' ' ')"
done
cat /sys/class/drm/version 2>/dev/null

sep "背光"
ls /sys/class/backlight/ 2>/dev/null
for b in /sys/class/backlight/*; do
    [ -d "$b" ] || continue
    echo "$b brightness=$(cat "$b/brightness" 2>/dev/null)/$(cat "$b/max_brightness" 2>/dev/null)"
done

sep "触摸"
grep -A4 -i "sec_ts" /proc/bus/input/devices 2>/dev/null | head -20
ls -l /dev/input/by-path/ 2>/dev/null | grep -i -E "touch|ts" || echo "(no touch by-path)"

sep "UDC / gadget"
ls /sys/class/udc/ 2>/dev/null || echo "(no UDC)"
GD=/sys/kernel/config/usb_gadget/tspi_udisp
if [ -d "$GD" ]; then
    echo "UDC = $(cat $GD/UDC 2>/dev/null)"
    echo "idVendor=$(cat $GD/idVendor 2>/dev/null) idProduct=$(cat $GD/idProduct 2>/dev/null)"
    echo "product=$(cat $GD/strings/0x409/product 2>/dev/null)"
else
    echo "(gadget $GD not created)"
fi
mount | grep functionfs || echo "(functionfs not mounted)"
lsusb 2>/dev/null | grep -i 303a || echo "(lsusb: no 303a device)"

sep "服务状态"
systemctl is-active tspi-usbmon 2>/dev/null
systemctl --no-pager --lines=10 status tspi-usbmon 2>/dev/null

sep "内核日志"
dmesg 2>/dev/null | grep -iE "dw-mipi-dsi|rockchip-drm|panel-simple|sec_ts|dwc3|udc|configfs|functionfs" | tail -30

sep "内存 / CMA / 温度"
grep -i cma /proc/meminfo 2>/dev/null
free -m 2>/dev/null
for t in /sys/class/thermal/thermal_zone*/temp; do
    [ -f "$t" ] && echo "$t = $(( $(cat "$t") / 1000 )) C"
done

sep "最近日志"
journalctl -u tspi-usbmon --no-pager -n 40 2>/dev/null

sep "完成"
echo "把以上输出（可脱敏）附在反馈里。"
