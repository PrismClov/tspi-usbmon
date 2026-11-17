#!/bin/sh
# 验证“解绑 → 恢复”的顺序：adbd 必须先重写 FunctionFS 描述符，再绑定 UDC。
# 结果写到 /tmp/um-restore.log，脚本结束后 adb 应自动回来。
exec >>/tmp/um-restore.log 2>&1
echo "=== start $(date +%H:%M:%S)"
G=/sys/kernel/config/usb_gadget/rockchip
UDC=$(cat $G/UDC 2>/dev/null)
echo "UDC=$UDC"
echo "" > $G/UDC
sleep 1
echo "unbound; adbd alive? $(pidof adbd || echo no)"
pidof adbd >/dev/null 2>&1 || start-stop-daemon --start --quiet --background --exec /usr/bin/adbd
sleep 2
echo "binding again"
echo "$UDC" > $G/UDC
echo "bind rc=$?"
sleep 2
echo "=== done $(date +%H:%M:%S)"
