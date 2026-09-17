#!/usr/bin/env bash
# Regenerate hicolor PNGs from the SVG sources (requires rsvg-convert, from librsvg2-bin).
set -euo pipefail
cd "$(dirname "$0")"
for size in 16 22 32 48 64 128 256 512; do
    src=org.relayterminal.Relay.svg
    [ "$size" -le 24 ] && src=org.relayterminal.Relay-small.svg
    mkdir -p "hicolor/${size}x${size}/apps"
    rsvg-convert -w "$size" -h "$size" "$src" -o "hicolor/${size}x${size}/apps/org.relayterminal.Relay.png"
done
