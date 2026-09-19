// SPDX-License-Identifier: AGPL-3.0-or-later
// Pixel-exact box drawing and block elements, so TUI borders (tmux, htop,
// mc) join seamlessly regardless of the font's glyph metrics.
#pragma once

#include <QColor>
#include <QPainter>
#include <QRect>

namespace relay {

// Draws `cp` into `cell` if it is a supported box-drawing (U+2500..U+257F) or
// block element (U+2580..U+259F) character. Returns false otherwise.
bool drawBoxCharacter(QPainter &p, char32_t cp, const QRect &cell, const QColor &color);

} // namespace relay
