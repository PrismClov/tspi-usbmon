#!/usr/bin/env python3
"""Generate the idle splash screen (1080x2160 portrait) for tspi-usbmon.

    ./scripts/make-splash.py [output.jpg]

SPDX-License-Identifier: MIT
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

W, H = 1080, 2160
CJK_FONTS = [
    "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
]
LATIN_FONTS = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
]


def pick(cands, fallback=None):
    for path in cands:
        if os.path.exists(path):
            return path
    return fallback


def font(path, size):
    try:
        return ImageFont.truetype(path, size)
    except OSError:
        return ImageFont.load_default()


def centered(draw, y, text, fnt, fill):
    width = draw.textlength(text, font=fnt)
    draw.text(((W - width) / 2, y), text, font=fnt, fill=fill)


def mixed(draw, y, segments, fill):
    """Draw segments [(text, font)] as one centered line (CJK fallback fonts
    have no Latin glyphs, so mixed lines are composed explicitly)."""
    widths = [draw.textlength(text, font=fnt) for text, fnt in segments]
    x = (W - sum(widths)) / 2
    for (text, fnt), w in zip(segments, widths):
        draw.text((x, y), text, font=fnt, fill=fill)
        x += w


def usb_glyph(draw, cx, cy, scale, color):
    """A simple USB trident drawn with lines and circles."""
    stem = int(210 * scale)
    draw.line([(cx, cy - stem // 2), (cx, cy + stem // 2)], fill=color, width=int(10 * scale))
    # connector on top
    hw = int(58 * scale)
    hh = int(34 * scale)
    draw.rounded_rectangle(
        [cx - hw, cy - stem // 2 - hh * 2, cx + hw, cy - stem // 2 - hh],
        radius=int(14 * scale),
        outline=color,
        width=int(9 * scale),
    )
    # left branch
    draw.line([(cx, cy + int(20 * scale)), (cx - int(105 * scale), cy - int(60 * scale))],
              fill=color, width=int(9 * scale))
    draw.ellipse(
        [cx - int(105 * scale) - int(22 * scale), cy - int(60 * scale) - int(22 * scale),
         cx - int(105 * scale) + int(22 * scale), cy - int(60 * scale) + int(22 * scale)],
        fill=color,
    )
    # right branch
    draw.line([(cx, cy + int(60 * scale)), (cx + int(105 * scale), cy - int(20 * scale))],
              fill=color, width=int(9 * scale))
    draw.regular_polygon(
        (cx + int(105 * scale), cy - int(20 * scale), int(20 * scale)), 4, rotation=45, fill=color
    )
    # bottom dot
    draw.ellipse(
        [cx - int(20 * scale), cy + stem // 2 - int(20 * scale),
         cx + int(20 * scale), cy + stem // 2 + int(20 * scale)],
        fill=color,
    )


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "assets/splash.jpg"

    img = Image.new("RGB", (W, H), (10, 14, 26))
    draw = ImageDraw.Draw(img)

    # vertical gradient + faint grid
    top = (8, 11, 22)
    bottom = (22, 28, 46)
    for y in range(H):
        t = y / (H - 1)
        color = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        draw.line([(0, y), (W, y)], fill=color)
    for y in range(0, H, 96):
        draw.line([(0, y), (W, y)], fill=(28, 34, 54), width=1)
    for x in range(0, W, 96):
        draw.line([(x, 0), (x, H)], fill=(24, 30, 48), width=1)

    accent = (94, 214, 255)
    dim = (120, 138, 168)
    white = (238, 244, 255)

    usb_glyph(draw, W // 2, int(H * 0.30), 1.55, accent)
    draw.ellipse(
        [W // 2 - 8, int(H * 0.30) - 8, W // 2 + 8, int(H * 0.30) + 8], fill=accent
    )

    latin_bold = pick(["/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"])
    latin = pick(LATIN_FONTS)
    cjk = pick(CJK_FONTS)

    centered(draw, int(H * 0.46), "USB  DISPLAY", font(latin_bold, 104), white)
    centered(draw, int(H * 0.53), "等待主机连接", font(cjk, 84), accent)
    centered(draw, int(H * 0.58), "WAITING  FOR  HOST", font(latin, 46), dim)

    # divider
    draw.line([(W // 2 - 180, int(H * 0.635)), (W // 2 + 180, int(H * 0.635))],
              fill=(44, 54, 80), width=3)

    mixed(draw, int(H * 0.66),
          [("LCKFB ", font(latin, 48)), ("泰山派", font(cjk, 48)),
           ("  ·  RK3566", font(latin, 48))], dim)
    centered(draw, int(H * 0.70), "TIANMA TL060FVXS07  ·  1080 × 2160", font(latin, 34),
             (86, 100, 126))

    # bottom hint
    mixed(draw, int(H * 0.90),
          [("插上 ", font(cjk, 40)), ("USB", font(latin, 40)),
           (" 线后自动切换为副屏", font(cjk, 40))], (96, 110, 136))

    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    img.save(out, quality=88, optimize=True, progressive=True)
    print(f"wrote {out} ({os.path.getsize(out) // 1024} KiB)")


if __name__ == "__main__":
    main()
