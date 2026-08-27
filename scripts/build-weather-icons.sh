#!/usr/bin/env bash
#
# Generate the bundled weather icons in data/wx/ from the Meteocons SVG set.
#
# Usage:
#   ./scripts/build-weather-icons.sh            # regenerate data/wx/
#   ./scripts/build-weather-icons.sh --preview  # also write a contact sheet
#
# Output is a '.wxi' per condition per size: a 4bpp palette-indexed still,
# decoded on the device by splitting nibbles (see include/display/IconBitmap.h
# for the layout). The format exists because nothing else fits -- AnimatedGIF
# wants 24KB in one heap block on a device that idles near 20KB free, and that
# allocation is what sank the animated icons in v1.3.2. A '.wxi' costs one row
# of pixels in RAM and nothing on the heap.
#
# Sixteen colours per icon is not a compromise here: the source art is flat
# with short gradients, so the quantised result is visually indistinguishable
# from the original at 80px.
#
# Palette entry 0 is forced to black, which is both the screen background and
# the reason drawing an icon also clears the slot it lands in.
#
# Requires: npm, rsvg-convert (librsvg), python3. Pillow is installed into a
# throwaway venv so the host python stays untouched.

set -euo pipefail

# Pinned: the upstream names and their artwork both drift, and a silent
# re-render would change every icon on the device without a diff to review.
readonly PKG="@bybas/weather-icons@2.0.0"
readonly PKG_TGZ="bybas-weather-icons-2.0.0.tgz"
readonly SVG_SUBDIR="package/production/fill/all"

readonly REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly OUT_DIR="${REPO_ROOT}/data/wx"
readonly LICENSE_OUT="${REPO_ROOT}/licenses/weather-icons-LICENSE"

# Icon basename -> upstream SVG name. The basenames are what
# DrawUtils::weatherIcon() builds its paths from, so they track the
# WeatherCondition enum; keep the two in step.
readonly DAY_ICONS=(
  "unknown:not-available"
  "clear:clear-day"
  "partly:partly-cloudy-day"
  "cloudy:overcast"
  "fog:fog"
  "drizzle:drizzle"
  "rain:rain"
  "snow:snow"
  "sleet:sleet"
  "thunder:thunderstorms"
)

# Only these two conditions differ after dark -- an overcast sky hides the sun
# and the moon equally. DrawUtils::weatherIconIsNight() encodes the same rule.
readonly NIGHT_ICONS=(
  "clear-n:clear-night"
  "partly-n:partly-cloudy-night"
)

# The weather clock's icon box, and the forecast screen's row icon. A forecast
# covers a whole day, so the 56px set carries no night variants.
readonly SIZE_FULL=80
readonly SIZE_ROW=56

PREVIEW=0
if [[ "${1:-}" == "--preview" ]]; then
  PREVIEW=1
elif [[ $# -gt 0 ]]; then
  echo "Unknown argument: $1" >&2
  echo "Usage: $0 [--preview]" >&2
  exit 2
fi

for tool in npm rsvg-convert python3; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Missing required tool: $tool" >&2
    exit 1
  fi
done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "==> Fetching ${PKG}"
(cd "$WORK" && npm pack "$PKG" >/dev/null 2>&1 && tar xzf "$PKG_TGZ")

readonly SVG_DIR="${WORK}/${SVG_SUBDIR}"
if [[ ! -d "$SVG_DIR" ]]; then
  echo "Package layout changed: ${SVG_SUBDIR} not found" >&2
  exit 1
fi

echo "==> Preparing Pillow"
python3 -m venv "${WORK}/venv" >/dev/null
"${WORK}/venv/bin/pip" install --quiet --disable-pip-version-check Pillow >/dev/null

# Attribution is a license requirement, not a courtesy.
mkdir -p "$(dirname "$LICENSE_OUT")"
cp "${WORK}/package/LICENSE" "$LICENSE_OUT"

render_set() {
  local size="$1"
  shift
  local dest="${OUT_DIR}/${size}"
  mkdir -p "$dest"

  local entry name svg
  for entry in "$@"; do
    name="${entry%%:*}"
    svg="${entry##*:}"

    if [[ ! -f "${SVG_DIR}/${svg}.svg" ]]; then
      echo "Missing upstream icon: ${svg}.svg" >&2
      exit 1
    fi

    rsvg-convert -w "$size" -h "$size" -b black \
      "${SVG_DIR}/${svg}.svg" -o "${WORK}/${name}-${size}.png"

    "${WORK}/venv/bin/python" "${WORK}/wxi.py" encode \
      "${WORK}/${name}-${size}.png" "${dest}/${name}.wxi"
  done
}

cat > "${WORK}/wxi.py" <<'PY_EOF'
"""Encode a PNG as '.wxi', and decode a set back into a contact sheet.

The decoder is here so the artwork can be checked without hardware: it reads
the bytes the device will read, so a mistake in the encoder shows up in the
preview rather than on the panel.
"""

import struct
import sys

from PIL import Image

MAGIC = b"WXI1"
BPP = 4
MAX_COLORS = 16


def to_rgb565(rgb):
    red, green, blue = rgb
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def from_rgb565(value):
    red = (value >> 8) & 0xF8
    green = (value >> 3) & 0xFC
    blue = (value << 3) & 0xF8
    # Replicate the high bits into the low ones, or every channel reads dark.
    return (red | (red >> 5), green | (green >> 6), blue | (blue >> 5))


def encode(src, dst):
    image = Image.open(src).convert("RGB")
    width, height = image.size

    # No dithering: Floyd-Steinberg noise is very visible at this pixel
    # density, and flat source art does not need it.
    quantized = image.quantize(
        colors=MAX_COLORS, method=Image.MEDIANCUT, dither=Image.Dither.NONE
    )

    flat = quantized.getpalette()[: MAX_COLORS * 3]
    palette = [tuple(flat[i * 3 : (i * 3) + 3]) for i in range(MAX_COLORS)]

    # Entry 0 must be the background: the device relies on it to clear the
    # slot. Median cut always yields black here because the render is
    # composited onto it, but find it rather than assume its position.
    darkest = min(range(MAX_COLORS), key=lambda i: sum(palette[i]))
    if darkest != 0:
        palette[0], palette[darkest] = palette[darkest], palette[0]
    palette[0] = (0, 0, 0)

    remap = {}
    for index in range(MAX_COLORS):
        remap[index] = index
    remap[0], remap[darkest] = darkest, 0

    indexed = [remap[value] for value in quantized.tobytes()]

    stride = (width + 1) // 2
    out = bytearray()
    out += MAGIC
    out += struct.pack("<BBBB", width, height, BPP, MAX_COLORS)
    for entry in palette:
        out += struct.pack("<H", to_rgb565(entry))

    for row in range(height):
        line = indexed[row * width : (row + 1) * width]
        for column in range(0, width, 2):
            high = line[column]
            low = line[column + 1] if column + 1 < width else 0
            out.append(((high & 0x0F) << 4) | (low & 0x0F))

    expected = 8 + (MAX_COLORS * 2) + (stride * height)
    if len(out) != expected:
        raise SystemExit(f"{dst}: wrote {len(out)} bytes, expected {expected}")

    with open(dst, "wb") as handle:
        handle.write(out)


def decode(src):
    with open(src, "rb") as handle:
        raw = handle.read()

    if raw[:4] != MAGIC:
        raise SystemExit(f"{src}: not a .wxi")

    width, height, bpp, count = struct.unpack("<BBBB", raw[4:8])
    if bpp != BPP:
        raise SystemExit(f"{src}: unexpected depth {bpp}")

    offset = 8
    palette = []
    for _ in range(count):
        (value,) = struct.unpack("<H", raw[offset : offset + 2])
        palette.append(from_rgb565(value))
        offset += 2

    stride = (width + 1) // 2
    image = Image.new("RGB", (width, height))
    for row in range(height):
        base = offset + (row * stride)
        for column in range(width):
            packed = raw[base + (column // 2)]
            index = (packed >> 4) if column % 2 == 0 else (packed & 0x0F)
            image.putpixel((column, row), palette[index])

    return image


def preview(paths, dst, scale=3, columns=6):
    tiles = [decode(path) for path in paths]
    cell = max(tile.width for tile in tiles) * scale
    rows = (len(tiles) + columns - 1) // columns
    sheet = Image.new("RGB", (cell * columns, cell * rows), (0, 0, 0))

    for position, tile in enumerate(tiles):
        scaled = tile.resize((tile.width * scale, tile.height * scale), Image.NEAREST)
        column = (position % columns) * cell
        row = (position // columns) * cell
        sheet.paste(scaled, (column, row))

    sheet.save(dst)


if __name__ == "__main__":
    if sys.argv[1] == "encode":
        encode(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "preview":
        preview(sys.argv[3:], sys.argv[2])
PY_EOF

echo "==> Rendering ${SIZE_FULL}px set"
render_set "$SIZE_FULL" "${DAY_ICONS[@]}" "${NIGHT_ICONS[@]}"

echo "==> Rendering ${SIZE_ROW}px set"
render_set "$SIZE_ROW" "${DAY_ICONS[@]}"

if [[ "$PREVIEW" == "1" ]]; then
  echo "==> Writing preview"
  mkdir -p "${REPO_ROOT}/build"
  "${WORK}/venv/bin/python" "${WORK}/wxi.py" preview \
    "${REPO_ROOT}/build/weather-icons-preview.png" \
    "${OUT_DIR}/${SIZE_FULL}"/*.wxi "${OUT_DIR}/${SIZE_ROW}"/*.wxi
  echo "    build/weather-icons-preview.png"
fi

echo "==> Done"
du -ch "${OUT_DIR}"/*/*.wxi | tail -1
