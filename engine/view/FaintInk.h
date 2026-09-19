// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Faint ink: what SGR 2 and a fold's dim rows are drawn in (#TK9C, #LG7T).
//
// Both used to be `fg.setAlphaF(0.6)`, which is a fade of 40% toward whatever is behind the cell.
// On the greys Relay's own themes use for secondary text that lands at about 2.9:1 on Relay Dark
// and 2.5:1 on Relay Light — under the 4.5:1 floor `docs/ARCHITECTURE.md` § 14 ("Legible text")
// sets for every piece of text a person reads. Here the ink is faded toward the cell's *own*
// background only as far as it can go while it still clears 4.5:1 against that background, and
// comes back opaque, so it does not depend on what happens to be painted under it.
//
// Two rules the callers rely on:
//   * ink that is already under 4.5:1 at full strength is returned unchanged — the helper never
//     makes text worse than the host asked for;
//   * the fade is measured against the background the cell is actually drawn on (a diff's tint, a
//     selection, the fold's own band), not against the terminal's default ground.
//
// Header-only and Qt-Gui-only on purpose: the engine must not reach into `src/`. It is called once
// per faint cell, and faint cells are rare (stats on a tool-call line, a fold's headings), so no
// cache is kept; the work is a dozen multiply-adds.
#include <QColor>

#include <algorithm>
#include <cmath>

namespace relay {

// WCAG 2.1 floor for text against its background.
inline constexpr double kTextContrast = 4.5;
// How far faint ink may fade when there is room for it: the 40% the old 0.6 alpha faded by, so
// nothing that was already legible changes.
inline constexpr double kFaintFade = 0.4;

// WCAG 2.1 relative luminance. Alpha is ignored: both arguments are colours on the screen.
inline double relativeLuminance(const QColor &c)
{
    const auto channel = [](double v) {
        return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
}

// WCAG 2.1 contrast ratio, 1.0 (identical) to 21.0 (black on white).
inline double contrastRatio(const QColor &a, const QColor &b)
{
    const double la = relativeLuminance(a), lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// `from` moved `t` of the way toward `to` (t = 0 is `from`, t = 1 is `to`), opaque.
inline QColor mixToward(const QColor &from, const QColor &to, double t)
{
    const auto c = [t](int a, int b) { return int(std::lround(a + (b - a) * t)); };
    return QColor(c(from.red(), to.red()), c(from.green(), to.green()), c(from.blue(), to.blue()));
}

// The faint form of `fg` on `bg`: as faded as it can be (up to `maxFade`) while it still reaches
// `kTextContrast` on `bg`. Fading toward `bg` only ever lowers the contrast, so the largest
// allowed fade is found by bisection. Ink that cannot reach the floor at full strength is handed
// back at full strength.
inline QColor faintInk(const QColor &fg, const QColor &bg, double maxFade = kFaintFade)
{
    const QColor solid(fg.red(), fg.green(), fg.blue());
    const QColor ground(bg.red(), bg.green(), bg.blue());
    if (maxFade <= 0.0 || contrastRatio(solid, ground) < kTextContrast)
        return solid;
    double keep = 0.0, drop = std::min(1.0, maxFade);
    for (int i = 0; i < 12; ++i) { // 12 halvings of a 0.4 range: finer than one 8-bit step
        const double mid = (keep + drop) / 2;
        if (contrastRatio(mixToward(solid, ground, mid), ground) >= kTextContrast)
            keep = mid;
        else
            drop = mid;
    }
    const QColor faded = mixToward(solid, ground, keep);
    return contrastRatio(faded, ground) >= kTextContrast ? faded : solid;
}

} // namespace relay
