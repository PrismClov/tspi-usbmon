#!/usr/bin/env bash
# 把面板 DTS、触摸驱动、内核配置片段集成进泰山派 Linux SDK。
#
#   SDK_DIR=~/tspi-linux-sdk ./scripts/kernel-integrate.sh [--build-dtb]
#
# SPDX-License-Identifier: MIT
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SDK_DIR=${SDK_DIR:-$HOME/tspi-linux-sdk}
KERNEL=$SDK_DIR/kernel
DTS_DIR=$KERNEL/arch/arm64/boot/dts/rockchip
CFG_DIR=$KERNEL/arch/arm64/configs
DTS=tspi-rk3566-user-v10-linux.dts
BUILD_DTB=0

for arg in "$@"; do
    case "$arg" in
        --build-dtb) BUILD_DTB=1 ;;
        -h|--help)
            sed -n '2,8p' "$0"
            exit 0
            ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

info() { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[!]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[x]\033[0m %s\n' "$*" >&2; exit 1; }

[ -d "$KERNEL" ] || die "找不到 kernel 目录：$KERNEL（请先 clone SDK 并设置 SDK_DIR=...）"
[ -f "$DTS_DIR/$DTS" ] || die "找不到 $DTS_DIR/$DTS，这个 SDK 看起来不是泰山派的 tspi-linux-sdk"

# ---------------------------------------------------------------- 1. DTS
if [ -f "$DTS_DIR/tspi-rk3566-dsi-v10.dtsi" ] && \
   [ ! -f "$DTS_DIR/tspi-rk3566-dsi-v10.dtsi.orig" ]; then
    cp "$DTS_DIR/tspi-rk3566-dsi-v10.dtsi" "$DTS_DIR/tspi-rk3566-dsi-v10.dtsi.orig"
    info "已备份原 dtsi 为 tspi-rk3566-dsi-v10.dtsi.orig"
fi
install -m644 "$HERE/kernel/dts/tspi-rk3566-dsi-v10-tianma.dtsi" \
    "$DTS_DIR/tspi-rk3566-dsi-v10.dtsi"
info "已写入天马 TL060FVXS07/08 面板 dtsi"

# 打开 dts 里的 include（默认是注释掉的）
python3 - "$DTS_DIR/$DTS" <<'PY'
import re
import sys

path = sys.argv[1]
text = open(path, encoding="utf-8").read()
pattern = re.compile(r'^[ \t]*//[ \t]*#include "tspi-rk3566-dsi-v10\.dtsi"', re.M)
if pattern.search(text):
    text = pattern.sub('#include "tspi-rk3566-dsi-v10.dtsi"', text, count=1)
    open(path, "w", encoding="utf-8").write(text)
    print("    include 已启用")
elif '#include "tspi-rk3566-dsi-v10.dtsi"' in text:
    print("    include 已启用（无需修改）")
else:
    sys.exit("在 %s 中找不到 dsi dtsi 的 include 行" % path)
PY
info "已启用 MIPI-DSI 面板节点"

# ---------------------------------------------------------------- 2. 触摸驱动
TS_DIR=$KERNEL/drivers/input/touchscreen
MY_TS=$TS_DIR/my_sec_ts
if [ ! -f "$MY_TS/sec_ts_main.c" ]; then
    "$HERE/scripts/fetch-touch-driver.sh" "$MY_TS"
else
    info "触摸驱动已存在：$MY_TS"
fi

python3 - "$TS_DIR/Kconfig" "$TS_DIR/Makefile" <<'PY'
import sys

kconfig, makefile = sys.argv[1], sys.argv[2]

text = open(kconfig, encoding="utf-8").read()
if "my_sec_ts/Kconfig" not in text:
    if "if INPUT_TOUCHSCREEN" not in text:
        sys.exit("Kconfig 里找不到 'if INPUT_TOUCHSCREEN'，请手动添加 source 行")
    text = text.replace(
        "if INPUT_TOUCHSCREEN",
        'if INPUT_TOUCHSCREEN\n\tsource "drivers/input/touchscreen/my_sec_ts/Kconfig"',
        1,
    )
    open(kconfig, "w", encoding="utf-8").write(text)
    print("    Kconfig 已添加 source 行")

text = open(makefile, encoding="utf-8").read()
if "my_sec_ts/" not in text:
    if not text.endswith("\n"):
        text += "\n"
    text += "obj-$(CONFIG_TOUCHSCREEN_SEC_TS_1223)\t+= my_sec_ts/\n"
    open(makefile, "w", encoding="utf-8").write(text)
    print("    Makefile 已添加 obj 行")
PY
info "触摸驱动已接入 Kconfig/Makefile"

# ---------------------------------------------------------------- 3. 内核配置
install -m644 "$HERE/kernel/configs/tspi-usbmon.config" "$CFG_DIR/tspi-usbmon.config"
info "已安装内核配置片段 arch/arm64/configs/tspi-usbmon.config"

shopt -s nullglob
for f in "$SDK_DIR"/device/rockchip/.chips/rk3566_rk3568/tspi-rk3566-*defconfig; do
    if grep -q 'tspi-usbmon.config' "$f"; then
        continue
    fi
    if grep -q '^RK_KERNEL_CFG_FRAGMENTS=' "$f"; then
        sed -i 's|^RK_KERNEL_CFG_FRAGMENTS="\([^"]*\)"|RK_KERNEL_CFG_FRAGMENTS="\1 tspi-usbmon.config"|' "$f"
        info "已把配置片段加入 $(basename "$f")"
    else
        warn "$(basename "$f") 没有 RK_KERNEL_CFG_FRAGMENTS，请手动合并 kernel/configs/tspi-usbmon.config"
    fi
done

# ---------------------------------------------------------------- 4. 可选编译 DTB
if [ "$BUILD_DTB" = 1 ]; then
    CROSS=""
    for c in "$SDK_DIR"/prebuilts/gcc/linux-x86/aarch64/*/bin/aarch64-*-gcc; do
        [ -x "$c" ] && CROSS=$c && break
    done
    if [ -z "$CROSS" ] && command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
        CROSS=$(command -v aarch64-linux-gnu-gcc)
    fi
    [ -n "$CROSS" ] || die "找不到 aarch64 交叉编译器，请设置 CROSS_COMPILE 后手动编译"

    if [ ! -f "$KERNEL/.config" ]; then
        warn "kernel/.config 不存在，先跑一次 ./build.sh kernel 再编 DTB；本次跳过"
    else
        info "编译 DTB：$DTS"
        make -C "$KERNEL" ARCH=arm64 CROSS_COMPILE="${CROSS%gcc}" dtbs
        ls -l "$DTS_DIR/$(basename "$DTS" .dts).dtb" 2>/dev/null || true
    fi
fi

cat <<EOF

下一步：
  1. 重新编译内核/固件（SDK 根目录）：
       cd $SDK_DIR
       ./build.sh tspi-rk3566-ubuntu-panfrost_defconfig
       ./build.sh
     或者只重编内核：./build.sh kernel
  2. 用 update.img 或分区小包烧录，详见 docs/03-kernel.md
  3. 上电后确认：ls /sys/class/drm/ 应能看到 card0-DSI-1
EOF
