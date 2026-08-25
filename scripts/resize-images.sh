#!/usr/bin/env bash
#
# Convert every image in a folder to the 240x240 the SmallTV Ultra panel
# expects, writing the results to a separate folder.
#
# Usage:
#   ./scripts/resize-images.sh <source-dir> [dest-dir] [--mode fit|fill] [--quality N]
#
# Defaults: dest-dir = <source-dir>/240, mode = fill, quality = 85
#
#   fill  crop to square, then scale - fills the screen, edges are trimmed
#   fit   scale to fit and pad with black - whole image visible, bars appear
#
# Output names are always sanitised and kept within 31 characters, which is all
# the device's LittleFS can store (LFS_NAME_MAX is 32 and it rejects anything
# at or beyond that). Pass --number for plain img001.jpg, img002.jpg naming.
#
# JPGs are re-encoded as JPG; GIFs stay animated GIFs. The upload page accepts
# both. Requires ImageMagick (`brew install imagemagick`).

set -euo pipefail

MODE="fill"
QUALITY="85"
NUMBER=0

# LittleFS on the ESP8266 refuses to create a name of 32 characters or more.
MAX_NAME=31
SRC=""
DEST=""

usage() {
    sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --mode)    MODE="${2:-}"; shift 2 ;;
        --quality) QUALITY="${2:-}"; shift 2 ;;
        --number)  NUMBER=1; shift ;;
        -h|--help) usage 0 ;;
        -*)        echo "Unknown option: $1" >&2; usage 1 ;;
        *)
            if [ -z "$SRC" ]; then SRC="$1"
            elif [ -z "$DEST" ]; then DEST="$1"
            else echo "Unexpected argument: $1" >&2; usage 1
            fi
            shift ;;
    esac
done

[ -n "$SRC" ] || usage 1

if [ ! -d "$SRC" ]; then
    echo "Source folder not found: $SRC" >&2
    exit 1
fi

case "$MODE" in
    fit|fill) ;;
    *) echo "Mode must be 'fit' or 'fill', got: $MODE" >&2; exit 1 ;;
esac

# ImageMagick 7 uses `magick`; 6 uses `convert`.
if command -v magick >/dev/null 2>&1; then
    IM="magick"
elif command -v convert >/dev/null 2>&1; then
    IM="convert"
else
    echo "ImageMagick not found. Install it with: brew install imagemagick" >&2
    exit 1
fi

DEST="${DEST:-$SRC/240}"
mkdir -p "$DEST"

# Shorten and sanitise a filename so the device will accept it.
# Keeps the head of the original name and appends a short checksum so two long
# names sharing a prefix cannot collapse onto the same short name.
shorten_name() {
    local stem="$1" ext="$2" full="$3"
    local safe tag room

    # Anything outside [A-Za-z0-9._-] becomes an underscore, runs collapse.
    safe="$(printf '%s' "$stem" | LC_ALL=C tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
    safe="${safe#_}"
    safe="${safe%_}"
    [ -n "$safe" ] || safe="img"

    if [ "${#safe}${ext}" ] && [ $(( ${#safe} + ${#ext} )) -le "$MAX_NAME" ]; then
        printf '%s%s' "$safe" "$ext"
        return
    fi

    tag="_$(printf '%s' "$full" | cksum | cut -d' ' -f1)"
    tag="${tag:0:5}"
    room=$(( MAX_NAME - ${#ext} - ${#tag} ))
    [ "$room" -lt 1 ] && room=1

    printf '%s%s%s' "${safe:0:$room}" "$tag" "$ext"
}

SIZE=240
converted=0
skipped=0
failed=0

# -print0 so names with spaces survive.
while IFS= read -r -d '' file; do
    base="$(basename "$file")"
    stem="${base%.*}"
    ext="${base##*.}"
    lower="$(printf '%s' "$ext" | tr '[:upper:]' '[:lower:]')"

    case "$lower" in
        jpg|jpeg|png|bmp|webp|gif) ;;
        *) skipped=$((skipped + 1)); continue ;;
    esac

    # Keep GIFs animated; everything else becomes a JPG the decoder handles.
    if [ "$lower" = "gif" ]; then
        target_ext=".gif"
    else
        target_ext=".jpg"
    fi

    if [ "$NUMBER" -eq 1 ]; then
        seq=$(( converted + failed + 1 ))
        outname="$(printf 'img%03d%s' "$seq" "$target_ext")"
    else
        outname="$(shorten_name "$stem" "$target_ext" "$base")"
    fi

    out="$DEST/$outname"

    # Don't read our own output back in if dest is inside src.
    [ "$file" -ef "$out" ] && continue

    if [ "$MODE" = "fill" ]; then
        geometry=(-resize "${SIZE}x${SIZE}^" -gravity center -extent "${SIZE}x${SIZE}")
    else
        geometry=(-resize "${SIZE}x${SIZE}" -gravity center
                  -background black -extent "${SIZE}x${SIZE}")
    fi

    if [ "$lower" = "gif" ]; then
        # coalesce/layers-optimize keeps multi-frame GIFs playable after resize.
        if "$IM" "$file" -coalesce "${geometry[@]}" -layers optimize "$out" 2>/dev/null; then
            converted=$((converted + 1))
            printf '  %-44s -> %s\n' "$base" "$outname"
        else
            failed=$((failed + 1))
            printf '  %-44s -> FAILED\n' "$base" >&2
        fi
    else
        if "$IM" "$file" "${geometry[@]}" -strip -quality "$QUALITY" "$out" 2>/dev/null; then
            converted=$((converted + 1))
            printf '  %-44s -> %s\n' "$base" "$outname"
        else
            failed=$((failed + 1))
            printf '  %-44s -> FAILED\n' "$base" >&2
        fi
    fi
done < <(find "$SRC" -maxdepth 1 -type f -print0)

echo
echo "Converted $converted, skipped $skipped, failed $failed"
echo "Output: $DEST"
echo
echo "Upload them from the Pictures tab - the folder picker takes the whole folder at once."

[ "$failed" -eq 0 ]
