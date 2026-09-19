// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>

#include <array>

namespace relay {

struct ColorScheme {
    QColor foreground{0xd8, 0xd8, 0xd8};
    QColor background{0x1c, 0x1e, 0x24};
    // When valid, the ground fades from background (top) to this colour (bottom). Cells with
    // their own background still paint solid; only the default ground is shaded.
    QColor backgroundEnd;
    QColor cursor{0xe0, 0xe0, 0xe0};
    QColor cursorText{0x1c, 0x1e, 0x24};
    QColor selection{0x3a, 0x5a, 0x8c};
    QColor searchMatch{0x6b, 0x5a, 0x1e};
    QColor searchCurrent{0xd0, 0x9a, 0x2a};
    QColor searchText{0x10, 0x10, 0x10};
    // "You can open this": the hover underline, OSC 8 and fold links, and (setLinksColouredAtRest)
    // every path or URL in the output. The host sets it from its theme's `[ui] link`.
    QColor link{0x12, 0xa4, 0x57};
    // The band under the shell's own prompt row (the row OSC 133;A marks, where the command the
    // user typed is echoed by the shell): painted behind every cell that has no background of its
    // own. Invalid (the default) = no band. The host sets it from its shell colour.
    QColor promptBand;
    // Folds (#TK9C): the tint behind an unfolded block and the rule down its
    // left edge. Invalid (the default) = mixed from background and foreground,
    // so a host that knows nothing about folds still gets a readable block.
    QColor foldBackground;
    QColor foldRule;
    // ANSI 0-15
    std::array<QRgb, 16> palette{{
        0xff1c1e24, 0xffe06c75, 0xff98c379, 0xffe5c07b, 0xff61afef, 0xffc678dd, 0xff56b6c2, 0xffd8d8d8,
        0xff5c6370, 0xffff7b86, 0xffb5e890, 0xffffd68a, 0xff7fc1ff, 0xffdf9df0, 0xff76d3de, 0xffffffff,
    }};

    // Palette entry 0..255 (16..255 are the standard xterm cube and grey ramp).
    QColor indexed(int i) const
    {
        if (i < 16)
            return QColor::fromRgb(palette[size_t(std::max(0, i))]);
        if (i < 232) {
            static const int steps[6] = {0, 95, 135, 175, 215, 255};
            const int n = i - 16;
            return QColor(steps[(n / 36) % 6], steps[(n / 6) % 6], steps[n % 6]);
        }
        const int v = 8 + 10 * (i - 232);
        return QColor(v, v, v);
    }
    uint32_t rgb(const QColor &c) const { return uint32_t(c.rgb() & 0xFFFFFF); }
};

} // namespace relay
