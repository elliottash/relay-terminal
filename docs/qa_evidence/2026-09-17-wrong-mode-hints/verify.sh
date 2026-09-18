#!/usr/bin/env bash
# OCR a screenshot (optionally a crop) and grep for text. Usage: verify.sh <png> [x y w h] <pattern>
set -u
png=$1; shift
if [[ $# -eq 5 ]]; then
    crop=( "$1" "$2" "$3" "$4" ); pattern=$5
    convert "$png" -crop "${crop[2]}x${crop[3]}+${crop[0]}+${crop[1]}" -resize 200% -colorspace Gray -negate /tmp/verify-crop.png
    png=/tmp/verify-crop.png
else
    pattern=$1
    convert "$png" -resize 150% -colorspace Gray -negate /tmp/verify-full.png
    png=/tmp/verify-full.png
fi
tesseract "$png" - --psm 6 2>/dev/null | grep -iE "$pattern"
