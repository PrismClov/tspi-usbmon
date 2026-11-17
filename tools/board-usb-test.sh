#!/bin/sh
exec >>/tmp/um-run.log 2>&1
echo "=== start $(date +%H:%M:%S) dur=${1:-30}"
# 在板子上临时把 UDC 让给 tspi-usbmon，测试完自动恢复原来的 adb gadget。
#   sh /tmp/um-test.sh [持续秒数]
# 注意：运行期间 adb 会断开，脚本结束（或看门狗超时）后自动恢复。
G=/sys/kernel/config/usb_gadget/rockchip
U=/sys/kernel/config/usb_gadget/tspi_udisp
LOG=/tmp/um-usb.log
DURATION=${1:-30}
UDC=$(cat $G/UDC 2>/dev/null)
[ -n "$UDC" ] || UDC=fcc00000.dwc3

restore() {
    [ -d "$U" ] && echo "" > $U/UDC 2>/dev/null
    sleep 1
    echo "$UDC" > $G/UDC 2>/dev/null
    sleep 1
    if ! pidof adbd >/dev/null 2>&1; then
        start-stop-daemon --start --quiet --background --exec /usr/bin/adbd 2>/dev/null
    fi
}
trap restore EXIT INT TERM

echo "unbinding stock gadget"
/tmp/tspi-usbmon -s takeover_udc=1 -s stream=1080x2160 -s rotate=0 -v > $LOG 2>&1 &
PID=$!
( sleep "$DURATION"; kill "$PID" 2>/dev/null ) &
echo "daemon pid $PID"
wait "$PID"
echo "daemon exited, restoring"
restore
echo "=== done $(date +%H:%M:%S)"
exit 0
