#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Regenerate the built-in per-condition weather animations in data/gif/.
#
# These ship inside littlefs.bin and are what "Match the weather" mode plays.
# The output is committed to the repo -- this script is the source of truth for
# it, the same way scripts/generate_openapi.py is for swagger.yml. Do not
# hand-edit data/gif/wx-*.gif.
#
# Source icons are Meteocons (https://github.com/basmilius/meteocons), MIT.
# See licenses/meteocons-LICENSE.
#
# Usage: ./scripts/build-weather-gifs.sh [work-dir]
#
# Requires: python3, ImageMagick 7 (magick), gifsicle, curl, tar.
# A throwaway venv holding python-lottie + cairosvg is built in the work dir.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORK=${1:-$ROOT/build/weather-gifs}
DEST=$ROOT/data/gif

# Pinned so a regeneration years from now produces the same icons.
MC_VERSION=0.1.0
MC_URL=https://registry.npmjs.org/@meteocons/lottie/-/lottie-$MC_VERSION.tgz
MC_STYLE=fill

# The panel slot is 80x80 (GIF_BOX in src/screens/WeatherScreens.cpp).
BOX=80

# Budget. The filesystem is 2MB and the web UI already spends ~248KB; these
# caps keep the icon set from eating the headroom users need for pictures.
# Blowing them is a build failure, not a warning -- see CLAUDE.md.
MAX_BYTES=20480
MAX_TOTAL=204800

# The decoder gives up on a single pass longer than GIF_MAX_MS_PER_FILE
# (src/display/Gif.cpp) and never restarts it, so a slow loop is a dead icon.
MAX_LOOP_MS=20000

# WeatherCondition (include/weather/WeatherClient.h) -> meteocons name -> output.
# Output names stay under the 31-char LittleFS ceiling (see CLAUDE.md).
MAP="
wx-unknown:not-available
wx-clear:clear-day
wx-partly-cloudy:partly-cloudy-day
wx-cloudy:overcast
wx-fog:fog
wx-drizzle:drizzle
wx-rain:rain
wx-snow:snow
wx-sleet:sleet
wx-thunder:thunderstorms-rain
"

die() { printf '%s\n' "error: $*" >&2; exit 1; }

for tool in python3 magick gifsicle curl tar; do
    command -v "$tool" >/dev/null 2>&1 || die "$tool not found"
done

mkdir -p "$WORK" "$DEST"

# --- dependencies -----------------------------------------------------------

VENV=$WORK/venv
LOTTIE=$VENV/bin/lottie_convert.py

if [ ! -x "$VENV/bin/python" ]; then
    printf 'creating venv in %s\n' "$VENV"
    python3 -m venv "$VENV" || die "could not create a venv"
fi

if [ ! -f "$LOTTIE" ]; then
    printf 'installing python-lottie + cairosvg\n'
    # cairosvg is what python-lottie rasterises through; without it the GIF
    # exporter reports "Unknown exporter" rather than failing usefully.
    "$VENV/bin/pip" install -q lottie cairosvg pillow \
        || die "pip install failed (cairosvg needs the native cairo library)"
fi

# --- source icons -----------------------------------------------------------

SRC=$WORK/meteocons
if [ ! -d "$SRC/package/$MC_STYLE" ]; then
    printf 'fetching meteocons %s\n' "$MC_VERSION"
    mkdir -p "$SRC"
    curl -fsSL "$MC_URL" -o "$WORK/meteocons.tgz" || die "could not fetch $MC_URL"
    tar xzf "$WORK/meteocons.tgz" -C "$SRC" || die "could not unpack meteocons"
fi

# --- render -----------------------------------------------------------------

# Number of animated properties in a lottie file, so a static source can be
# told apart from a failed render.
animated_props() {
    "$VENV/bin/python" -c 'import json,sys
d=json.dumps(json.load(open(sys.argv[1])))
print(d.count(chr(34)+"a"+chr(34)+": 1") + d.count(chr(34)+"a"+chr(34)+":1"))' "$1"
}

# Rendered at one quality rung, returning the byte size of $DEST/$1.gif.
#   $1 output stem  $2 source stem  $3 frame skip  $4 colour count
render() {
    out=$DEST/$1.gif
    src=$SRC/package/$MC_STYLE/$2.json
    raw=$WORK/$1-raw.gif
    flat=$WORK/$1-flat.gif
    prepped=$WORK/$1-src.json

    [ -f "$src" ] || die "$2 is not in meteocons $MC_STYLE"

    # cairosvg resolves lottie layer masks to fully transparent, which silently
    # erases masked layers -- partly-cloudy-day loses its sun entirely. The
    # masks only trim each sun layer where the cloud already covers it, and the
    # cloud is the topmost layer, so dropping them restores the sun without
    # changing what you see. Only partly-cloudy-day carries any.
    "$VENV/bin/python" -c 'import json,sys
d = json.load(open(sys.argv[1]))
for layer in d["layers"]:
    layer.pop("hasMask", None)
    layer.pop("masksProperties", None)
json.dump(d, open(sys.argv[2], "w"))' "$src" "$prepped" || die "could not preprocess $2"

    # --gif-skip-frames decimates the 60fps source; --fps is ignored by this
    # exporter, which is why the frame rate is dialled in here instead.
    "$VENV/bin/python" "$LOTTIE" \
        --gif-skip-frames "$3" --width "$BOX" --height "$BOX" \
        "$prepped" "$raw" >/dev/null 2>&1 || die "lottie render failed for $2"

    # A few meteocons icons (not-available) carry no animated properties and
    # correctly render to one frame. Anything that *should* move but came out
    # static means python-lottie choked on the file, which must not ship
    # silently as a still image.
    frames=$(magick identify "$raw" | wc -l | tr -d ' ')
    if [ "$frames" -le 1 ] && [ "$(animated_props "$src")" -gt 0 ]; then
        die "$2 rendered as a single frame but its source is animated -- \
python-lottie could not handle it. Render the frames with a headless \
lottie-web instead and rebuild the GIF from them."
    fi

    # The decoder has no alpha and composites transparency onto black, so bake
    # the background in rather than leaning on GIF disposal at runtime.
    magick "$raw" -coalesce -background black -alpha remove -alpha off \
        -resize "${BOX}x${BOX}" -colors "$4" -layers optimize "$flat" \
        || die "flatten failed for $1"

    gifsicle -O3 --lossy=60 --colors "$4" -o "$out" "$flat" 2>/dev/null \
        || die "gifsicle failed for $1"

    stat -f%z "$out" 2>/dev/null || stat -c%s "$out"
}

total=0

printf '%-22s %7s %7s %6s\n' icon frames bytes loop

echo "$MAP" | while IFS=: read -r stem source; do
    [ -n "$stem" ] || continue

    # Quality ladder. Each rung costs animation smoothness before it costs
    # colour, because at 80px the palette is what carries these icons.
    size=$(render "$stem" "$source" 6 64)
    [ "$size" -le "$MAX_BYTES" ] || size=$(render "$stem" "$source" 8 64)
    [ "$size" -le "$MAX_BYTES" ] || size=$(render "$stem" "$source" 10 32)
    [ "$size" -le "$MAX_BYTES" ] || size=$(render "$stem" "$source" 12 32)

    out=$DEST/$stem.gif
    frames=$(magick identify "$out" | wc -l | tr -d ' ')
    geom=$(magick identify -format '%wx%h' "$out[0]")
    loop=$(magick identify -format '%T ' "$out" | awk '{for(i=1;i<=NF;i++)s+=$i; print s*10}')

    printf '%-22s %7s %7s %5sms\n' "$stem.gif" "$frames" "$size" "$loop"

    [ "$geom" = "${BOX}x${BOX}" ] || die "$stem.gif is $geom, expected ${BOX}x${BOX}"
    [ "$size" -le "$MAX_BYTES" ] || die "$stem.gif is $size bytes, over the ${MAX_BYTES}B cap"
    [ "$loop" -le "$MAX_LOOP_MS" ] || die \
        "$stem.gif loops in ${loop}ms, over the ${MAX_LOOP_MS}ms the decoder allows"

    total=$((total + size))
    echo "$total" > "$WORK/.total"
done

total=$(cat "$WORK/.total")
printf '%-22s %7s %7s\n' TOTAL '' "$total"
[ "$total" -le "$MAX_TOTAL" ] || die "the set is $total bytes, over the ${MAX_TOTAL}B cap"

printf 'wrote %s\n' "$DEST"
