#!/usr/bin/env python3
"""Generates the WHDSelector logo: chrome letters in an 80s/90s demoscene
style (blue-white-gold chrome, shaded red outline, black background).

Everything is drawn at SS times the final size and scaled down, which
gives anti-aliased edges; 32 colours keep the gradients smooth.

Writes tools/logo_preview.png and src/gui/logo_data.c (interleaved
bitplanes = ILBM BODY layout for MUI's Bodychunk class).

Run in Docker:
  docker run --rm -v "$PWD:/work" -w /work alpine:3.20 sh -c \
    "apk add -q python3 py3-pillow font-dejavu && python3 tools/make_logo.py"
"""
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops

TEXT = "WHDSelector"
W, H = 416, 72            # width must be a multiple of 16 (BODY rows)
DEPTH = 5                 # 32 colours
SS = 4                    # supersampling factor
FONT = "/usr/share/fonts/dejavu/DejaVuSans-BoldOblique.ttf"


def text_mask(w, h):
    """Text rendered big and fitted to w x h (grey-scale, soft edges kept)."""
    font = ImageFont.truetype(FONT, 400)
    probe = Image.new("L", (1, 1))
    x0, y0, x1, y1 = ImageDraw.Draw(probe).textbbox((0, 0), TEXT, font=font)
    big = Image.new("L", (x1 - x0 + 40, y1 - y0 + 40), 0)
    ImageDraw.Draw(big).text((20 - x0, 20 - y0), TEXT, font=font, fill=255)
    big = big.crop(big.getbbox())
    return big.resize((w, h), Image.LANCZOS)


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def gradient(h, stops):
    rows = []
    for y in range(h):
        t = y / max(1, h - 1)
        for i in range(len(stops) - 1):
            t0, c0 = stops[i]
            t1, c1 = stops[i + 1]
            if t0 <= t <= t1:
                # smoothstep for softer transitions
                u = (t - t0) / (t1 - t0)
                u = u * u * (3 - 2 * u)
                rows.append(lerp(c0, c1, u))
                break
    return rows


CHROME = [
    (0.00, (18, 36, 96)),
    (0.22, (60, 110, 200)),
    (0.42, (175, 215, 255)),
    (0.49, (250, 252, 255)),
    (0.52, (70, 50, 80)),
    (0.60, (130, 70, 35)),
    (0.80, (230, 165, 70)),
    (1.00, (255, 235, 180)),
]

OUTLINE = [
    (0.00, (255, 90, 80)),
    (0.50, (210, 20, 25)),
    (1.00, (110, 0, 10)),
]


def gradient_image(w, h, top, bottom, stops):
    rows = gradient(bottom - top, stops)
    img = Image.new("RGB", (w, h))
    line = Image.new("RGB", (w, 1))
    for y in range(h):
        c = rows[min(len(rows) - 1, max(0, y - top))]
        line.paste(c, (0, 0, w, 1))
        img.paste(line, (0, y))
    return img


def build():
    w, h = W * SS, H * SS
    tw, th = w - 40 * SS, h - 22 * SS
    mask = text_mask(tw, th)
    ox, oy = (w - tw) // 2 - 3 * SS, (h - th) // 2 - 3 * SS

    letters = Image.new("L", (w, h), 0)
    letters.paste(mask, (ox, oy))

    def grow(m, px):
        # MaxFilter needs an odd size; grow in several steps for big radii
        out = m
        r = px * SS
        while r > 0:
            step = min(r, 8)
            out = out.filter(ImageFilter.MaxFilter(step * 2 + 1))
            r -= step
        return out

    outline = grow(letters, 2)
    rim = grow(letters, 3)
    shadow = ImageChops.offset(grow(letters, 3), 5 * SS, 5 * SS).filter(ImageFilter.GaussianBlur(2 * SS))

    img = Image.new("RGB", (w, h), (0, 0, 0))
    img.paste((16, 16, 60), (0, 0), shadow)
    img.paste((60, 0, 8), (0, 0), rim)
    img.paste(gradient_image(w, h, oy - 2 * SS, oy + th + 2 * SS, OUTLINE), (0, 0), outline)
    img.paste(gradient_image(w, h, oy, oy + th, CHROME), (0, 0), letters)

    # Soft highlight along the upper-left edge of the letters.
    edge = ImageChops.subtract(letters, ImageChops.offset(letters, SS, SS))
    edge = edge.filter(ImageFilter.GaussianBlur(SS * 0.6)).point(lambda v: int(v * 0.8))
    img.paste((255, 255, 255), (0, 0), edge)

    return img.resize((W, H), Image.LANCZOS)


def to_planar(img):
    """Quantise to 2^DEPTH colours with black as colour 0; return data."""
    q = img.quantize(colors=1 << DEPTH, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[: 3 * (1 << DEPTH)]
    pal += [0] * (3 * (1 << DEPTH) - len(pal))
    colours = [tuple(pal[i * 3: i * 3 + 3]) for i in range(1 << DEPTH)]

    darkest = min(range(len(colours)), key=lambda i: sum(colours[i]))
    order = [darkest] + [i for i in range(len(colours)) if i != darkest]
    remap = {old: new for new, old in enumerate(order)}
    colours = [colours[i] for i in order]
    data = [remap[v] for v in q.getdata()]

    row_bytes = ((W + 15) // 16) * 2
    body = bytearray()
    for y in range(H):
        row = data[y * W:(y + 1) * W]
        for plane in range(DEPTH):
            for bx in range(row_bytes):
                byte = 0
                for bit in range(8):
                    x = bx * 8 + bit
                    if x < W and (row[x] >> plane) & 1:
                        byte |= 0x80 >> bit
                body.append(byte)
    preview = Image.new("RGB", (W, H))
    preview.putdata([colours[v] for v in data])
    return colours, bytes(body), preview


def write_c(colours, body, path):
    with open(path, "w") as f:
        f.write("/* Generated by tools/make_logo.py - do not edit. */\n")
        f.write('#include <exec/types.h>\n#include "gui/logo_data.h"\n\n')
        f.write("const ULONG logo_colors[%d] = {\n" % (3 * len(colours)))
        for r, g, b in colours:
            f.write("    0x%08lX, 0x%08lX, 0x%08lX,\n" % (r * 0x01010101, g * 0x01010101, b * 0x01010101))
        f.write("};\n\nconst UBYTE logo_body[%d] = {\n" % len(body))
        for i in range(0, len(body), 16):
            f.write("    " + ", ".join("0x%02X" % b for b in body[i:i + 16]) + ",\n")
        f.write("};\n")


def write_h(path):
    with open(path, "w") as f:
        f.write("#ifndef GUI_LOGO_DATA_H\n#define GUI_LOGO_DATA_H\n\n#include <exec/types.h>\n\n")
        f.write("/* WHDSelector logo (generated by tools/make_logo.py): uncompressed\n"
                " * ILBM BODY, interleaved bitplanes, and a 32-bit-per-gun RGB palette\n"
                " * for MUIA_Bitmap_SourceColors. */\n")
        f.write("#define LOGO_WIDTH  %d\n#define LOGO_HEIGHT %d\n#define LOGO_DEPTH  %d\n\n" % (W, H, DEPTH))
        f.write("extern const ULONG logo_colors[3 << LOGO_DEPTH];\nextern const UBYTE logo_body[];\n\n#endif\n")


if __name__ == "__main__":
    image = build()
    colours, body, preview = to_planar(image)
    preview.resize((W * 2, H * 2), Image.NEAREST).save("tools/logo_preview.png")
    write_c(colours, body, "src/gui/logo_data.c")
    write_h("src/gui/logo_data.h")
    print("logo %dx%dx%d, body %d bytes" % (W, H, DEPTH, len(body)))
