#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Measured contrast for Relay's theme files (data/theme/themes/*.toml).

Every pair below is one the app actually paints, with the colours it actually
uses: the "on" colours are computed exactly as src/Theme.cpp computes them
(inkOn(), the caution blend), not guessed. WCAG 2.1 relative luminance.

    contrast.py check [theme-id ...]     # pass/fail per theme; exit 1 on a failure
    contrast.py table <theme-id>         # markdown, every pair
    contrast.py compare                  # all themes side by side
    contrast.py ratio '#3ec5f0' '#fff'   # ad hoc

Rules (the second column of CONTRACT):
    AA    4.5:1   text a person reads
    UI    3:1     an edge or glyph a person must find (WCAG 1.4.11)
    deco  none    carries no information on its own

Plus one rule WCAG does not have, for the dark-copper trap:
    distinct  CIELAB dE76 >= 20 between a warm chrome colour and a meaning
              colour it could be mistaken for (copper vs amber, copper vs red).
"""
from __future__ import annotations

import colorsys
import math
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
THEMES = ROOT / "data" / "theme" / "themes"
ORDER = ["relay-dark", "dark-copper", "gruvbox-dark", "solarized-dark", "relay-light", "ibm-beige"]
NEW = {"dark-copper", "ibm-beige"}

# --- colour maths ----------------------------------------------------------------


def rgb(value: str) -> tuple[int, int, int]:
    text = value.strip().lstrip("#")
    if len(text) == 3:
        text = "".join(c * 2 for c in text)
    if len(text) != 6:
        raise ValueError(f"not a #rrggbb colour: {value!r}")
    return tuple(int(text[i : i + 2], 16) for i in (0, 2, 4))  # type: ignore[return-value]


def hexa(c: tuple[int, int, int]) -> str:
    return "#%02x%02x%02x" % c


def luminance(c: tuple[int, int, int]) -> float:
    def ch(v: int) -> float:
        s = v / 255
        return s / 12.92 if s <= 0.03928 else ((s + 0.055) / 1.055) ** 2.4

    r, g, b = (ch(v) for v in c)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def ratio(a, b) -> float:
    a = rgb(a) if isinstance(a, str) else a
    b = rgb(b) if isinstance(b, str) else b
    la, lb = luminance(a), luminance(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def lab(c: tuple[int, int, int]) -> tuple[float, float, float]:
    def lin(v: int) -> float:
        s = v / 255
        return s / 12.92 if s <= 0.04045 else ((s + 0.055) / 1.055) ** 2.4

    r, g, b = (lin(v) for v in c)
    x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047
    y = 0.2126 * r + 0.7152 * g + 0.0722 * b
    z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883

    def f(t: float) -> float:
        return t ** (1 / 3) if t > 0.008856 else 7.787 * t + 16 / 116

    fx, fy, fz = f(x), f(y), f(z)
    return 116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)


def delta_e(a, b) -> float:
    a = rgb(a) if isinstance(a, str) else a
    b = rgb(b) if isinstance(b, str) else b
    return math.dist(lab(a), lab(b))


def blend(a, b, weight_of_a: float) -> tuple[int, int, int]:
    """src/Theme.cpp blend(): a*w + b*(1-w), rounded."""
    w = max(0.0, min(1.0, weight_of_a))
    return tuple(int(round(x * w + y * (1 - w))) for x, y in zip(a, b))  # type: ignore[return-value]


def ink_on(fill: tuple[int, int, int]) -> tuple[int, int, int]:
    """src/Theme.cpp inkOn(): the fill's own hue at lightness 22/255 or 242/255."""
    r, g, b = fill
    light = (r * 299 + g * 587 + b * 114) // 1000 > 140
    h, l, s = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
    s = min(s, 200 / 255)
    l = (22 if light else 242) / 255
    out = colorsys.hls_to_rgb(h, l, s)
    return tuple(int(round(v * 255)) for v in out)  # type: ignore[return-value]


def darker(c: tuple[int, int, int], factor: int) -> tuple[int, int, int]:
    """QColor::darker(factor): HSV value divided by factor/100."""
    h, s, v = colorsys.rgb_to_hsv(*(x / 255 for x in c))
    out = colorsys.hsv_to_rgb(h, s, v * 100 / factor)
    return tuple(int(round(x * 255)) for x in out)  # type: ignore[return-value]


# --- themes ----------------------------------------------------------------------


def load(theme_id: str) -> dict[str, tuple[int, int, int]]:
    """The colours the app paints for a theme, including the derived ones."""
    data = tomllib.loads((THEMES / f"{theme_id}.toml").read_text(encoding="utf-8"))
    ui, syn, term = data["ui"], data.get("syntax", {}), data["terminal"]
    c: dict[str, tuple[int, int, int]] = {}
    for key, value in ui.items():
        c[key] = rgb(value)
    c.setdefault("shell", c["accent"])
    c.setdefault("agent", rgb("#b48ef7"))
    c.setdefault("success", rgb("#7ec88c"))
    c.setdefault("warning", rgb("#e5c07b"))
    c.setdefault("error", rgb("#e06c75"))
    c.setdefault("selection", darker(c["accent"], 200))
    for key, value in syn.items():
        c[f"syntax.{key}"] = rgb(value)
    c["term.bg"] = rgb(term["background"])
    c["term.fg"] = rgb(term["foreground"])
    if "cursor" in term:
        c["term.cursor"] = rgb(term["cursor"])
    for i, value in enumerate(term["palette"]):
        c[f"ansi{i}"] = rgb(value)
    # Derived exactly as src/Theme.cpp stylesheetFor() derives them.
    c["caution"] = blend(c["warning"], c["error"], 0.7)
    for name in ("shell", "agent", "success", "warning", "error", "caution", "selection"):
        c[f"on.{name}"] = ink_on(c[name])
    c["_variant"] = data["theme"].get("variant", "dark")  # type: ignore[assignment]
    c["_name"] = data["theme"]["name"]  # type: ignore[assignment]
    return c


# (foreground, background, rule, what a person reads there)
CONTRACT = [
    ("text", "background", "AA", "body text on the window ground"),
    ("text", "surface", "AA", "text in editors, previews, the idle composer"),
    ("text", "surface_raised", "AA", "text on chips, menus, the focused composer"),
    ("text_muted", "background", "AA", "cwd, hints, notification bodies"),
    ("text_muted", "surface", "AA", "secondary text on the text surface"),
    ("text_muted", "surface_raised", "AA", "chip labels: the whole status strip"),
    ("accent", "background", "AA", "accent used as text (the route label, the running queue)"),
    ("accent_text", "accent", "AA", "a primary button's label"),
    ("shell", "surface_raised", "AA", "the mode chip reading TERMINAL"),
    ("agent", "surface_raised", "AA", "the mode chip reading AGENT"),
    ("shell", "background", "AA", "the shell destination as text"),
    ("agent", "background", "AA", "the agent destination as text; agent prompt echo"),
    ("on.shell", "shell", "AA", "the agent prefix chip (ink on the shell fill)"),
    ("on.agent", "agent", "AA", "the plan chip (ink on the agent fill)"),
    ("on.warning", "warning", "AA", "the shell prefix chip (ink on amber)"),
    ("on.caution", "caution", "AA", "the secret chip"),
    ("on.selection", "selection", "AA", "selected text in every input"),
    ("success", "surface_raised", "AA", "the tasks chip, done"),
    ("warning", "surface_raised", "AA", "the tasks chip, attention; warn labels"),
    ("error", "surface_raised", "AA", "an error on a chip"),
    ("success", "background", "AA", "a success dot on a notification row"),
    ("warning", "background", "AA", "a warning dot on a notification row"),
    ("error", "background", "AA", "an error dot on a notification row"),
    ("border_strong", "background", "UI", "the focused pane's outline"),
    ("border", "background", "deco", "a hairline rule"),
    # `link` (2026-09-19): "you can open this" — a path in the output, a fold row, a Markdown link.
    ("link", "background", "AA", "a link in the chrome (Sessions page, notifications)"),
    ("link", "surface", "AA", "the composer's path token, idle"),
    ("link", "surface_raised", "AA", "the composer's path token, focused"),
    ("link", "term.bg", "AA", "a path or URL in program output, at rest"),
]
SYNTAX = ["command", "unknown", "flag", "string", "path", "operator", "variable", "agent", "token"]
for _s in SYNTAX:
    CONTRACT.append((f"syntax.{_s}", "surface", "AA", f"composer {_s}, idle"))
    CONTRACT.append((f"syntax.{_s}", "surface_raised", "AA", f"composer {_s}, focused (you are typing)"))
CONTRACT += [
    ("term.fg", "term.bg", "AA", "terminal output"),
    ("term.cursor", "term.bg", "UI", "the cursor block"),
]
# ANSI 0 is a background colour in practice: exempt. ANSI 8 is the colour programs
# pick in order to be de-emphasised: UI 3:1, or it stops being dim. The rest AA.
for _i in list(range(1, 8)) + list(range(9, 16)):
    CONTRACT.append((f"ansi{_i}", "term.bg", "AA", f"ANSI {_i} as text"))
CONTRACT.append(("ansi8", "term.bg", "UI", "ANSI 8, the dim colour"))

# Warm chrome must not be mistakable for a meaning colour. Checked on every theme,
# asserted on the new ones.
#
# accent == shell is deliberate in the incumbents (the accent *means* shell in
# Relay's visual language), so it is not checked. shell vs agent is: the
# destination pair must stay two colours even when a theme greys it down.
DISTINCT = [
    ("link", "shell"), ("link", "agent"), ("link", "error"), ("link", "warning"), ("link", "success"),
    ("accent", "warning"), ("accent", "error"),
    ("border_strong", "warning"), ("border_strong", "error"),
    ("surface_raised", "warning"), ("border", "error"),
    ("shell", "agent"),
]
RULES = {"AA": 4.5, "UI": 3.0, "deco": 0.0}
DISTINCT_MIN = 20.0


def audit(c):
    rows = []
    for fg, bg, rule, what in CONTRACT:
        if fg not in c or bg not in c:
            continue
        r = ratio(c[fg], c[bg])
        rows.append((fg, bg, rule, r, r >= RULES[rule] - 1e-9, what))
    return rows


def distinct(c):
    return [(a, b, delta_e(c[a], c[b])) for a, b in DISTINCT if a in c and b in c]


def main(argv: list[str]) -> int:
    cmd = argv[1] if len(argv) > 1 else "check"
    if cmd == "ratio":
        print(f"{ratio(argv[2], argv[3]):.2f}:1   dE {delta_e(argv[2], argv[3]):.1f}")
        return 0
    if cmd == "table":
        c = load(argv[2])
        print("| foreground | on | rule | measured | |")
        print("|---|---|---|---|---|")
        for fg, bg, rule, r, ok, what in audit(c):
            print(f"| `{fg}` `{hexa(c[fg])}` | `{bg}` `{hexa(c[bg])}` | {rule} | **{r:.2f}:1** | "
                  f"{'pass' if ok else 'FAIL'} — {what} |")
        print()
        print("| distinctness | ΔE76 | |")
        print("|---|---|---|")
        for a, b, d in distinct(c):
            print(f"| `{a}` vs `{b}` | {d:.1f} | {'ok' if d >= DISTINCT_MIN else 'CLOSE'} |")
        return 0
    if cmd == "compare":
        themes = [t for t in ORDER if (THEMES / f"{t}.toml").exists()]
        cs = {t: load(t) for t in themes}
        print("| pair | rule | " + " | ".join(themes) + " |")
        print("|---|---|" + "---|" * len(themes))
        for fg, bg, rule, what in CONTRACT:
            if rule == "deco":
                continue
            cells = []
            for t in themes:
                c = cs[t]
                if fg not in c or bg not in c:
                    cells.append("—")
                    continue
                r = ratio(c[fg], c[bg])
                mark = "" if r >= RULES[rule] else " ✗"
                cells.append(f"{r:.2f}{mark}")
            print(f"| `{fg}` on `{bg}` | {rule} | " + " | ".join(cells) + " |")
        return 0

    themes = argv[2:] or [t for t in ORDER if (THEMES / f"{t}.toml").exists()]
    failures_new = 0
    for t in themes:
        c = load(t)
        rows = audit(c)
        bad = [r for r in rows if not r[4] and r[2] != "deco"]
        close = [d for d in distinct(c) if d[2] < DISTINCT_MIN]
        graded = [r for r in rows if r[2] != "deco"]
        worst = min(graded, key=lambda r: r[3])
        worst_aa = min((r for r in graded if r[2] == "AA"), key=lambda r: r[3])
        tag = "NEW " if t in NEW else "    "
        print(f"{tag}{t:<15} {len(graded):>3} pairs  worst {worst[3]:5.2f} ({worst[0]} on {worst[1]}, {worst[2]})"
              f"  worst AA {worst_aa[3]:5.2f} ({worst_aa[0]} on {worst_aa[1]})"
              f"  {'ok' if not bad else str(len(bad)) + ' FAIL'}")
        for fg, bg, rule, r, _ok, what in bad:
            print(f"      FAIL {fg} on {bg}: {r:.2f}:1 < {RULES[rule]}:1 ({rule}) — {what}")
        for a, b, d in close:
            print(f"      close: {a} vs {b} dE {d:.1f} < {DISTINCT_MIN}")
        if t in NEW:
            failures_new += len(bad) + len(close)
    # Only the auditioned themes gate the exit code: the incumbents are measured and
    # reported, not changed (owner, 2026-09-18).
    return 1 if failures_new else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
