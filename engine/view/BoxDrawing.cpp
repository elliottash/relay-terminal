// SPDX-License-Identifier: GPL-3.0-or-later
#include "BoxDrawing.h"

#include <algorithm>

namespace relay {
namespace {

// Arm weights: 0 none, 1 light, 2 heavy, 3 double.
struct Arms {
    uint8_t left, right, up, down;
};

bool armsFor(char32_t cp, Arms *a)
{
    switch (cp) {
    case 0x2500: *a = {1, 1, 0, 0}; return true; // ─
    case 0x2501: *a = {2, 2, 0, 0}; return true; // ━
    case 0x2502: *a = {0, 0, 1, 1}; return true; // │
    case 0x2503: *a = {0, 0, 2, 2}; return true; // ┃
    case 0x250C: *a = {0, 1, 0, 1}; return true; // ┌
    case 0x250D: *a = {0, 2, 0, 1}; return true;
    case 0x250E: *a = {0, 1, 0, 2}; return true;
    case 0x250F: *a = {0, 2, 0, 2}; return true; // ┏
    case 0x2510: *a = {1, 0, 0, 1}; return true; // ┐
    case 0x2511: *a = {2, 0, 0, 1}; return true;
    case 0x2512: *a = {1, 0, 0, 2}; return true;
    case 0x2513: *a = {2, 0, 0, 2}; return true; // ┓
    case 0x2514: *a = {0, 1, 1, 0}; return true; // └
    case 0x2515: *a = {0, 2, 1, 0}; return true;
    case 0x2516: *a = {0, 1, 2, 0}; return true;
    case 0x2517: *a = {0, 2, 2, 0}; return true; // ┗
    case 0x2518: *a = {1, 0, 1, 0}; return true; // ┘
    case 0x2519: *a = {2, 0, 1, 0}; return true;
    case 0x251A: *a = {1, 0, 2, 0}; return true;
    case 0x251B: *a = {2, 0, 2, 0}; return true; // ┛
    case 0x251C: *a = {0, 1, 1, 1}; return true; // ├
    case 0x251D: *a = {0, 2, 1, 1}; return true;
    case 0x2520: *a = {0, 1, 2, 2}; return true;
    case 0x2523: *a = {0, 2, 2, 2}; return true; // ┣
    case 0x2524: *a = {1, 0, 1, 1}; return true; // ┤
    case 0x2525: *a = {2, 0, 1, 1}; return true;
    case 0x2528: *a = {1, 0, 2, 2}; return true;
    case 0x252B: *a = {2, 0, 2, 2}; return true; // ┫
    case 0x252C: *a = {1, 1, 0, 1}; return true; // ┬
    case 0x252F: *a = {2, 2, 0, 1}; return true;
    case 0x2530: *a = {1, 1, 0, 2}; return true;
    case 0x2533: *a = {2, 2, 0, 2}; return true; // ┳
    case 0x2534: *a = {1, 1, 1, 0}; return true; // ┴
    case 0x2537: *a = {2, 2, 1, 0}; return true;
    case 0x2538: *a = {1, 1, 2, 0}; return true;
    case 0x253B: *a = {2, 2, 2, 0}; return true; // ┻
    case 0x253C: *a = {1, 1, 1, 1}; return true; // ┼
    case 0x253F: *a = {2, 2, 1, 1}; return true;
    case 0x2542: *a = {1, 1, 2, 2}; return true;
    case 0x254B: *a = {2, 2, 2, 2}; return true; // ╋
    case 0x2550: *a = {3, 3, 0, 0}; return true; // ═
    case 0x2551: *a = {0, 0, 3, 3}; return true; // ║
    case 0x2552: *a = {0, 3, 0, 1}; return true;
    case 0x2553: *a = {0, 1, 0, 3}; return true;
    case 0x2554: *a = {0, 3, 0, 3}; return true; // ╔
    case 0x2555: *a = {3, 0, 0, 1}; return true;
    case 0x2556: *a = {1, 0, 0, 3}; return true;
    case 0x2557: *a = {3, 0, 0, 3}; return true; // ╗
    case 0x2558: *a = {0, 3, 1, 0}; return true;
    case 0x2559: *a = {0, 1, 3, 0}; return true;
    case 0x255A: *a = {0, 3, 3, 0}; return true; // ╚
    case 0x255B: *a = {3, 0, 1, 0}; return true;
    case 0x255C: *a = {1, 0, 3, 0}; return true;
    case 0x255D: *a = {3, 0, 3, 0}; return true; // ╝
    case 0x255E: *a = {0, 3, 1, 1}; return true;
    case 0x255F: *a = {0, 1, 3, 3}; return true;
    case 0x2560: *a = {0, 3, 3, 3}; return true; // ╠
    case 0x2561: *a = {3, 0, 1, 1}; return true;
    case 0x2562: *a = {1, 0, 3, 3}; return true;
    case 0x2563: *a = {3, 0, 3, 3}; return true; // ╣
    case 0x2564: *a = {3, 3, 0, 1}; return true;
    case 0x2565: *a = {1, 1, 0, 3}; return true;
    case 0x2566: *a = {3, 3, 0, 3}; return true; // ╦
    case 0x2567: *a = {3, 3, 1, 0}; return true;
    case 0x2568: *a = {1, 1, 3, 0}; return true;
    case 0x2569: *a = {3, 3, 3, 0}; return true; // ╩
    case 0x256A: *a = {3, 3, 1, 1}; return true;
    case 0x256B: *a = {1, 1, 3, 3}; return true;
    case 0x256C: *a = {3, 3, 3, 3}; return true; // ╬
    case 0x256D: *a = {0, 1, 0, 1}; return true; // ╭ (square corner)
    case 0x256E: *a = {1, 0, 0, 1}; return true; // ╮
    case 0x256F: *a = {1, 0, 1, 0}; return true; // ╯
    case 0x2570: *a = {0, 1, 1, 0}; return true; // ╰
    case 0x2574: *a = {1, 0, 0, 0}; return true;
    case 0x2575: *a = {0, 0, 1, 0}; return true;
    case 0x2576: *a = {0, 1, 0, 0}; return true;
    case 0x2577: *a = {0, 0, 0, 1}; return true;
    case 0x2578: *a = {2, 0, 0, 0}; return true;
    case 0x2579: *a = {0, 0, 2, 0}; return true;
    case 0x257A: *a = {0, 2, 0, 0}; return true;
    case 0x257B: *a = {0, 0, 0, 2}; return true;
    case 0x257C: *a = {1, 2, 0, 0}; return true;
    case 0x257D: *a = {0, 0, 1, 2}; return true;
    case 0x257E: *a = {2, 1, 0, 0}; return true;
    case 0x257F: *a = {0, 0, 2, 1}; return true;
    default: return false;
    }
}

} // namespace

bool drawBoxCharacter(QPainter &p, char32_t cp, const QRect &cell, const QColor &color)
{
    const int x = cell.left(), y = cell.top(), w = cell.width(), h = cell.height();
    if (cp >= 0x2580 && cp <= 0x259F) {
        auto fill = [&](int fx, int fy, int fw, int fh, int alpha = 255) {
            QColor c = color;
            c.setAlpha(alpha);
            p.fillRect(QRect(fx, fy, fw, fh), c);
        };
        switch (cp) {
        case 0x2580: fill(x, y, w, h / 2); return true;                 // ▀
        case 0x2588: fill(x, y, w, h); return true;                     // █
        case 0x258C: fill(x, y, w / 2, h); return true;                 // ▌
        case 0x2590: fill(x + w / 2, y, w - w / 2, h); return true;     // ▐
        case 0x2591: fill(x, y, w, h, 64); return true;                 // ░
        case 0x2592: fill(x, y, w, h, 128); return true;                // ▒
        case 0x2593: fill(x, y, w, h, 192); return true;                // ▓
        case 0x2594: fill(x, y, w, std::max(1, h / 8)); return true;    // ▔
        case 0x2595: fill(x + w - std::max(1, w / 8), y, std::max(1, w / 8), h); return true; // ▕
        default: break;
        }
        if (cp >= 0x2581 && cp <= 0x2587) { // ▁..▇ lower n/8
            const int eighths = int(cp - 0x2580);
            const int fh = (h * eighths + 4) / 8;
            fill(x, y + h - fh, w, fh);
            return true;
        }
        if (cp >= 0x2589 && cp <= 0x258F) { // ▉..▏ left n/8
            const int eighths = 8 - int(cp - 0x2588);
            fill(x, y, (w * eighths + 4) / 8, h);
            return true;
        }
        // Quadrants U+2596..U+259F: bits UL, UR, LL, LR.
        static const uint8_t quads[10] = {
            0x4, // ▖ LL
            0x8, // ▗ LR
            0x1, // ▘ UL
            0x1 | 0x4 | 0x8, // ▙ UL LL LR
            0x1 | 0x8, // ▚ UL LR
            0x1 | 0x2 | 0x4, // ▛ UL UR LL
            0x1 | 0x2 | 0x8, // ▜ UL UR LR
            0x2, // ▝ UR
            0x2 | 0x4, // ▞ UR LL
            0x2 | 0x4 | 0x8, // ▟ UR LL LR
        };
        if (cp >= 0x2596) {
            const uint8_t q = quads[cp - 0x2596];
            const int hw = w / 2, hh = h / 2;
            if (q & 0x1) fill(x, y, hw, hh);
            if (q & 0x2) fill(x + hw, y, w - hw, hh);
            if (q & 0x4) fill(x, y + hh, hw, h - hh);
            if (q & 0x8) fill(x + hw, y + hh, w - hw, h - hh);
            return true;
        }
        return false;
    }

    Arms a;
    if (!armsFor(cp, &a))
        return false;
    const int light = std::max(1, w / 8);
    const int heavy = std::max(light + 1, w / 4);
    const int cx = x + (w - light) / 2; // light line left edge (vertical)
    const int cy = y + (h - light) / 2; // light line top edge (horizontal)
    const int midX = x + w / 2, midY = y + h / 2;
    const int gap = std::max(1, light); // spacing between double lines

    auto hArm = [&](int weight, int x0, int x1) {
        if (!weight || x1 <= x0)
            return;
        if (weight == 3) {
            p.fillRect(QRect(x0, cy - gap, x1 - x0, light), color);
            p.fillRect(QRect(x0, cy + gap, x1 - x0, light), color);
        } else {
            const int t = weight == 2 ? heavy : light;
            p.fillRect(QRect(x0, y + (h - t) / 2, x1 - x0, t), color);
        }
    };
    auto vArm = [&](int weight, int y0, int y1) {
        if (!weight || y1 <= y0)
            return;
        if (weight == 3) {
            p.fillRect(QRect(cx - gap, y0, light, y1 - y0), color);
            p.fillRect(QRect(cx + gap, y0, light, y1 - y0), color);
        } else {
            const int t = weight == 2 ? heavy : light;
            p.fillRect(QRect(x + (w - t) / 2, y0, t, y1 - y0), color);
        }
    };
    // Arms overlap the centre by half a line width so corners are closed.
    const int over = std::max(heavy, light + 2 * gap);
    hArm(a.left, x, std::min(x + w, midX + over / 2 + 1));
    hArm(a.right, std::max(x, midX - over / 2), x + w);
    vArm(a.up, y, std::min(y + h, midY + over / 2 + 1));
    vArm(a.down, std::max(y, midY - over / 2), y + h);
    return true;
}

} // namespace relay
