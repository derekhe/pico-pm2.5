"""Generate compact 1-bit firmware fonts from an installed CJK font.

The generated header is checked in, so Pillow and the Windows font are not
required to build the firmware.
"""

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "generated" / "fonts_generated.hpp"
FONT_CANDIDATES = [
    Path(r"C:\Windows\Fonts\msyh.ttc"),
    Path(r"C:\Windows\Fonts\simhei.ttf"),
    Path("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"),
]

CHINESE = "空气优良轻度污染中严重预估参考传感器断开预热秒颗粒物明细趋势设备状态当前平均最低最高小时有效帧校验错误重同步运行时间分钟无数据已连接固件范围返回未知"
SYMBOLS = "μ³"
ASCII = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"
SMALL_GLYPHS = "".join(sorted(set(ASCII + CHINESE + SYMBOLS), key=ord))
LARGE_GLYPHS = " -0123456789"


def locate_font() -> Path:
    for candidate in FONT_CANDIDATES:
        if candidate.exists():
            return candidate
    raise SystemExit("No supported CJK font found")


def render_font(font_path: Path, size: int, characters: str):
    font = ImageFont.truetype(str(font_path), size=size, index=0)
    ascent, descent = font.getmetrics()
    bitmap = bytearray()
    glyphs = []
    for character in characters:
        bbox = font.getbbox(character, anchor="ls")
        advance = max(1, min(255, round(font.getlength(character)) + 1))
        if bbox is None or character == " ":
            glyphs.append((ord(character), len(bitmap), 0, 0, advance, 0, 0))
            continue
        width = max(1, bbox[2] - bbox[0])
        height = max(1, bbox[3] - bbox[1])
        image = Image.new("1", (width, height), 0)
        draw = ImageDraw.Draw(image)
        draw.text((-bbox[0], -bbox[1]), character, font=font, fill=1, anchor="ls")
        row_bytes = (width + 7) // 8
        offset = len(bitmap)
        for y in range(height):
            for byte_x in range(row_bytes):
                value = 0
                for bit in range(8):
                    x = byte_x * 8 + bit
                    if x < width and image.getpixel((x, y)):
                        value |= 0x80 >> bit
                bitmap.append(value)
        glyphs.append((ord(character), offset, width, height, advance, bbox[0], ascent + bbox[1]))
    return glyphs, bitmap, ascent + descent


def emit_array(values, indent="    ", per_line=16):
    lines = []
    for start in range(0, len(values), per_line):
        chunk = values[start : start + per_line]
        lines.append(indent + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    return "\n".join(lines)


def main():
    font_path = locate_font()
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    sections = [
        "#pragma once",
        "",
        '#include "pm25/font.hpp"',
        "",
        "namespace pm25 {",
        "",
    ]
    for name, size, glyph_set in (
        ("font14", 14, SMALL_GLYPHS),
        ("font18", 18, SMALL_GLYPHS),
        ("font24", 24, SMALL_GLYPHS),
        ("font64", 64, LARGE_GLYPHS),
    ):
        glyphs, bitmap, line_height = render_font(font_path, size, glyph_set)
        sections.append(f"inline constexpr std::uint8_t {name}_bitmap[] = {{")
        sections.append(emit_array(bitmap))
        sections.append("};")
        sections.append(f"inline constexpr FontGlyph {name}_glyphs[] = {{")
        for codepoint, offset, width, height, advance, x_offset, y_offset in glyphs:
            sections.append(
                f"    {{{codepoint}U, {offset}U, {width}U, {height}U, {advance}U, "
                f"{x_offset}, {y_offset}}},"
            )
        sections.append("};")
        sections.append(
            f"inline constexpr Font {name}{{{name}_glyphs, "
            f"sizeof({name}_glyphs) / sizeof({name}_glyphs[0]), {name}_bitmap, {line_height}U}};"
        )
        sections.append("")
    sections.append("}  // namespace pm25")
    OUTPUT.write_text("\n".join(sections) + "\n", encoding="utf-8", newline="\n")
    print(f"Generated {OUTPUT} from {font_path}")


if __name__ == "__main__":
    main()
