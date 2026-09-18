#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write app/pane-theme.css, the web pane's colours and widget styles, from the desktop's own theme.

The web view of a pane (app/pane.js) has to look like the Qt pane, and a theme change has to reach
both. So nothing in app/pane-theme.css is typed by hand: this script reads

  * src/Theme.h        the compiled-in token defaults and the three legible text sizes;
  * src/Theme.cpp      the stylesheet (`stylesheetFor`), the derived `@tokens` it substitutes and
                       the local colours they are computed from (`selection`, `caution`), and the
                       monospace family list (`monoFamily`);
  * src/Pane.h         the one colour the queue list paints itself (a selected row);
  * data/theme/themes/relay-dark.toml and relay-light.toml, the two built-in themes whose values
                       Theme.cpp's tokens take at run time,

and evaluates the same expressions Qt does (`inkOn`, `blend`, `withAlpha`, QColor::lighter and
::darker, with Qt's own 16-bit and float arithmetic), so `--rt-accent-soft` here is the exact
colour the desktop paints.

Two kinds of custom property come out:

  --rt-<token>             every `@token` of the stylesheet (`@accentBorder` → --rt-accent-border),
                           every Theme.h colour, the terminal palette and the text sizes;
  --qss-<widget>-<prop>    what Qt's cascade gives each widget the pane view draws: the `#toast`,
                           `#queueTitle`, `#queueHint`, `#thinkingOverlay`, `#composer`, `#stripChip`
                           … rules, with their type rules (`QLabel { … }`) folded in the way Qt
                           folds them. A state rule (`:hover`, `[dest="agent"]`) becomes
                           --qss-<widget>-<state>-<prop>.

Point sizes become `calc(var(--rt-pt) * N)`: app/pane.css sets --rt-pt per device (one desktop point
on a laptop, more on a phone) in rem, so the browser's zoom and text-size settings still apply.

Relay Dark is the default (`.relay-pane`); `.relay-pane[data-theme="relay-light"]` switches.

    scripts/gen-web-theme.py            regenerate app/pane-theme.css
    scripts/gen-web-theme.py --check    exit 1 if it is stale (tests/test_web_theme.py does this)
    scripts/gen-web-theme.py --qss relay-dark   print the substituted stylesheet, to compare with
                                                qApp->styleSheet() in a running Relay
"""
from __future__ import annotations

import argparse
import re
import struct
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = Path("app/pane-theme.css")
THEMES = ("relay-dark", "relay-light")   # the default, then the one a light desktop uses
USHRT_MAX = 0xFFFF

# The widgets the web pane draws, as Qt sees them: (css name, Qt class, object name, ancestors).
# The cascade below gives each one what the desktop's stylesheet gives the real widget.
WIDGETS = [
    ("pane", "QWidget", "pane", []),
    ("toast", "QLabel", "toast", ["pane"]),
    ("thinkingOverlay", "QFrame", "thinkingOverlay", ["pane"]),
    ("transcriptHeader", "QLabel", "transcriptHeader", ["pane", "thinkingOverlay"]),
    ("thinkingView", "QPlainTextEdit", "thinkingView", ["pane", "thinkingOverlay"]),
    ("queueStrip", "QFrame", "queueStrip", ["pane"]),
    ("queueTitle", "QLabel", "queueTitle", ["pane", "queueStrip"]),
    ("queueHint", "QLabel", "queueHint", ["pane", "queueStrip"]),
    ("queueRunning", "QLabel", "queueRunning", ["pane", "queueStrip"]),
    ("queueItem", "QLabel", "queueItem", ["pane", "queueStrip"]),
    ("queueSteer", "QLabel", "queueSteer", ["pane", "queueStrip"]),
    ("queueList", "QListWidget", "queueList", ["pane", "queueStrip"]),
    ("queueButton", "QToolButton", "", ["pane", "queueStrip"]),
    ("composer", "QFrame", "composer", ["pane"]),
    ("composerEditor", "QPlainTextEdit", "composerEditor", ["pane", "composer"]),
    ("stripChip", "QToolButton", "stripChip", ["pane", "composer"]),
    ("stripChipLabel", "QLabel", "stripChipLabel", ["pane", "composer"]),
    ("statusPicker", "QComboBox", "statusPicker", ["pane", "composer"]),
    ("contextLabel", "QLabel", "contextLabel", ["pane", "composer"]),
    ("keyCap", "QLabel", "keyCap", []),
    ("button", "QPushButton", "", []),
    ("menu", "QMenu", "", []),
    ("tooltip", "QToolTip", "", []),
]

# Enough of Qt's class tree for a type selector to match a subclass, as QSS does.
BASES = {
    "QLabel": ["QLabel", "QFrame", "QWidget"],
    "QFrame": ["QFrame", "QWidget"],
    "QPlainTextEdit": ["QPlainTextEdit", "QAbstractScrollArea", "QFrame", "QWidget"],
    "QToolButton": ["QToolButton", "QAbstractButton", "QWidget"],
    "QPushButton": ["QPushButton", "QAbstractButton", "QWidget"],
    "QComboBox": ["QComboBox", "QWidget"],
    "QListWidget": ["QListWidget", "QListView", "QAbstractItemView", "QAbstractScrollArea", "QFrame", "QWidget"],
    "QMenu": ["QMenu", "QWidget"],
    "QToolTip": ["QToolTip"],
    "QWidget": ["QWidget"],
}

# Declarations that mean nothing in a browser or that the pane view does not take from Qt.
SKIP_PROPERTIES = {"outline", "image", "subcontrol-origin", "left", "spacing",
                   "selection-background-color", "selection-color"}


class ThemeError(Exception):
    pass


# --- Qt's colour arithmetic ----------------------------------------------------------------------
# QColor keeps 16-bit components and converts through float; these follow qcolor.cpp step by step
# so a derived colour comes out on the same 8-bit value, not one off.

def f32(x: float) -> float:
    return struct.unpack("f", struct.pack("f", x))[0]


def qround(x: float) -> int:
    return int(x + 0.5) if x >= 0 else int(x - 0.5)


def div257(x: int) -> int:
    return (x - (x >> 8) + 0x80) >> 8


class Color:
    """A QColor in the Rgb spec: 16-bit red, green, blue and alpha."""

    def __init__(self, r16: int, g16: int, b16: int, a16: int = USHRT_MAX):
        self.r16, self.g16, self.b16, self.a16 = r16, g16, b16, a16

    @classmethod
    def rgb(cls, r: int, g: int, b: int, a: int = 255) -> "Color":
        return cls(r * 0x101, g * 0x101, b * 0x101, a * 0x101)

    @classmethod
    def parse(cls, text: str) -> "Color":
        value = text.strip()
        if re.fullmatch(r"#[0-9a-fA-F]{6}", value):
            return cls.rgb(int(value[1:3], 16), int(value[3:5], 16), int(value[5:7], 16))
        if re.fullmatch(r"#[0-9a-fA-F]{8}", value):   # QColor reads #AARRGGBB
            return cls.rgb(int(value[3:5], 16), int(value[5:7], 16), int(value[7:9], 16),
                           int(value[1:3], 16))
        raise ThemeError(f"not a colour this generator reads: {text!r}")

    @property
    def red(self): return div257(self.r16)
    @property
    def green(self): return div257(self.g16)
    @property
    def blue(self): return div257(self.b16)
    @property
    def alpha(self): return div257(self.a16)

    def hex(self) -> str:
        return f"#{self.red:02x}{self.green:02x}{self.blue:02x}"

    def css_rgba(self) -> str:
        alpha = round(self.alpha / 255, 3)
        return f"rgba({self.red}, {self.green}, {self.blue}, {alpha:g})"

    # QColor::toHsv
    def _hsv(self):
        r, g, b = (f32(c / USHRT_MAX) for c in (self.r16, self.g16, self.b16))
        mx, mn = max(r, g, b), min(r, g, b)
        delta = f32(mx - mn)
        value = qround(f32(mx * USHRT_MAX))
        if abs(delta) <= 0.00001:
            return USHRT_MAX, 0, value
        sat = qround(f32(f32(delta / mx) * USHRT_MAX))
        if r == mx:
            hue = f32((g - b) / delta)
        elif g == mx:
            hue = f32(2.0 + f32((b - r) / delta))
        else:
            hue = f32(4.0 + f32((r - g) / delta))
        hue = f32(hue * 60.0)
        if hue < 0:
            hue = f32(hue + 360.0)
        return qround(f32(hue * 100.0)), sat, value

    # QColor::convertTo(Rgb) from Hsv
    @classmethod
    def _from_hsv(cls, hue: int, sat: int, value: int, alpha: int) -> "Color":
        if sat == 0 or hue == USHRT_MAX:
            return cls(value, value, value, alpha)
        h = 0.0 if hue == 36000 else f32(hue / 6000.0)
        s = f32(sat / USHRT_MAX)
        v = f32(value / USHRT_MAX)
        i = int(h)
        f = f32(h - i)
        p = f32(v * f32(1.0 - s))
        if i & 1:
            q = f32(v * f32(1.0 - f32(s * f)))
            r, g, b = {1: (q, v, p), 3: (p, q, v), 5: (v, p, q)}[i]
        else:
            t = f32(v * f32(1.0 - f32(s * f32(1.0 - f))))
            r, g, b = {0: (v, t, p), 2: (p, v, t), 4: (t, p, v)}[i]
        return cls(*(qround(f32(c * USHRT_MAX)) for c in (r, g, b)), alpha)

    def lighter(self, factor: int) -> "Color":
        if factor <= 0:
            return self
        if factor < 100:
            return self.darker(10000 // factor)
        hue, sat, value = self._hsv()
        value = (factor * value) // 100
        if value > USHRT_MAX:
            sat = max(0, sat - (value - USHRT_MAX))
            value = USHRT_MAX
        return Color._from_hsv(hue, sat, value, self.a16)

    def darker(self, factor: int) -> "Color":
        if factor <= 0:
            return self
        if factor < 100:
            return self.lighter(10000 // factor)
        hue, sat, value = self._hsv()
        return Color._from_hsv(hue, sat, (value * 100) // factor, self.a16)

    # QColor::toHsl, hslHue(), hslSaturation()
    def _hsl(self):
        r, g, b = (f32(c / USHRT_MAX) for c in (self.r16, self.g16, self.b16))
        mx, mn = max(r, g, b), min(r, g, b)
        delta = f32(mx - mn)
        total = f32(mx + mn)
        lightness = qround(f32(f32(total * 0.5) * USHRT_MAX))
        if abs(delta) <= 0.00001:
            return USHRT_MAX, 0, lightness
        if total <= 1.0:
            sat = qround(f32(f32(delta / total) * USHRT_MAX))
        else:
            sat = qround(f32(f32(delta / f32(2.0 - total)) * USHRT_MAX))
        if r == mx:
            hue = f32((g - b) / delta)
        elif g == mx:
            hue = f32(2.0 + f32((b - r) / delta))
        else:
            hue = f32(4.0 + f32((r - g) / delta))
        hue = f32(hue * 60.0)
        if hue < 0:
            hue = f32(hue + 360.0)
        return qround(f32(hue * 100.0)), sat, lightness

    # QColor::setHsl(h, s, l) then toRgb()
    @classmethod
    def from_hsl8(cls, h: int, s: int, l: int, alpha: int) -> "Color":
        hue = USHRT_MAX if h == -1 else (h % 360) * 100
        sat, light = s * 0x101, l * 0x101
        if sat == 0 or hue == USHRT_MAX:
            return cls(light, light, light, alpha)
        hh = 0.0 if hue == 36000 else f32(hue / 36000.0)
        ss = f32(sat / USHRT_MAX)
        ll = f32(light / USHRT_MAX)
        temp2 = f32(ll * f32(1.0 + ss)) if ll < 0.5 else f32(f32(ll + ss) - f32(ll * ss))
        temp1 = f32(f32(2.0 * ll) - temp2)
        third = f32(1.0 / 3.0)
        out = []
        for t in (f32(hh + third), hh, f32(hh - third)):
            if t < 0.0:
                t = f32(t + 1.0)
            elif t > 1.0:
                t = f32(t - 1.0)
            six = f32(t * 6.0)
            if six < 1.0:
                c = f32(temp1 + f32(f32(temp2 - temp1) * six))
            elif f32(t * 2.0) < 1.0:
                c = temp2
            elif f32(t * 3.0) < 2.0:
                c = f32(temp1 + f32(f32(f32(temp2 - temp1) * f32(f32(2.0 / 3.0) - t)) * 6.0))
            else:
                c = temp1
            out.append(qround(f32(c * USHRT_MAX)))
        return cls(*out, alpha)

    def hsl_hue(self) -> int:
        hue, _, _ = self._hsl()
        return -1 if hue == USHRT_MAX else hue // 100

    def hsl_saturation(self) -> int:
        return div257(self._hsl()[1])


def ink_on(fill: Color) -> Color:
    """Theme.cpp inkOn(): a very dark or very light tint of the fill."""
    light = (fill.red * 299 + fill.green * 587 + fill.blue * 114) // 1000 > 140
    return Color.from_hsl8(fill.hsl_hue(), min(fill.hsl_saturation(), 200), 22 if light else 242,
                           fill.a16)


def blend(a: Color, b: Color, weight: float) -> Color:
    w = min(max(weight, 0.0), 1.0)
    mix = lambda x, y: int(round_half_away(x * w + y * (1 - w)))
    return Color.rgb(mix(a.red, b.red), mix(a.green, b.green), mix(a.blue, b.blue))


def round_half_away(x: float) -> float:   # std::lround
    return float(int(x + 0.5)) if x >= 0 else float(int(x - 0.5))


def with_alpha(c: Color, alpha: int) -> Color:
    return Color(c.r16, c.g16, c.b16, alpha * 0x101)


# --- reading the C++ -----------------------------------------------------------------------------

def read_defaults(theme_h: str) -> dict[str, Color]:
    """`inline QColor Name{0x.., 0x.., 0x..};` from Theme.h."""
    out = {}
    for name, r, g, b in re.findall(
            r"inline QColor (\w+)\{(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+)\}", theme_h):
        out[name] = Color.rgb(int(r, 16), int(g, 16), int(b, 16))
    if "Background" not in out or "Agent" not in out:
        raise ThemeError("src/Theme.h: no `inline QColor Name{…}` tokens found")
    return out


def read_text_sizes(theme_h: str) -> dict[str, float]:
    sizes = dict((name, float(value)) for name, value in re.findall(
        r"inline constexpr qreal (\w+Pt) = ([0-9.]+);", theme_h))
    for name in ("BodyPt", "SecondaryPt", "FloorPt"):
        if name not in sizes:
            raise ThemeError(f"src/Theme.h: {name} not found")
    return sizes


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise ThemeError(f"src/Theme.cpp: `{signature}` not found")
    depth, i = 0, source.index("{", start)
    for j in range(i, len(source)):
        if source[j] == "{":
            depth += 1
        elif source[j] == "}":
            depth -= 1
            if depth == 0:
                return source[i + 1:j]
    raise ThemeError(f"src/Theme.cpp: unbalanced braces in `{signature}`")


def read_adopt(theme_cpp: str) -> tuple[list[tuple[str, str, str]], list[tuple[str, str, str]]]:
    """adoptTokens(): which theme-file key each live token takes, and its fallback expression."""
    body = function_body(theme_cpp, "void adoptTokens(")
    ui = re.findall(r"(\w+) = ui\(\"(\w+)\", (\w+)\);", body)
    syntax = re.findall(r"(\w+) = syntax\(\"(\w+)\", (\w+)\);", body)
    if not ui:
        raise ThemeError("src/Theme.cpp: adoptTokens() has no `X = ui(\"key\", Y);` lines")
    return ui, syntax


def read_stylesheet(theme_cpp: str) -> str:
    body = function_body(theme_cpp, "QString stylesheetFor(")
    match = re.search(r'R"\((.*?)\)"', body, re.S)
    if not match:
        raise ThemeError("src/Theme.cpp: stylesheetFor() has no raw string")
    return match.group(1)


def read_token_table(theme_cpp: str) -> tuple[list[tuple[str, str]], list[tuple[str, str]]]:
    """The locals stylesheetFor() computes, and its `{QStringLiteral("@x"), expr}` table."""
    body = function_body(theme_cpp, "QString stylesheetFor(")
    locals_ = re.findall(r"const (?:QColor|QString) (\w+) = (.+?);\n", body)
    table_start = body.find("tokens{")
    if table_start < 0:
        raise ThemeError("src/Theme.cpp: stylesheetFor() has no `tokens{` table")
    depth, i = 0, body.index("{", table_start)
    for j in range(i, len(body)):
        depth += {"{": 1, "}": -1}.get(body[j], 0)
        if depth == 0:
            table = body[i + 1:j]
            break
    pairs = []
    k = 0
    while True:
        m = re.compile(r'\{QStringLiteral\("(@\w+)"\),\s*').search(table, k)
        if not m:
            break
        depth, start = 0, m.end()
        for j in range(start, len(table)):
            ch = table[j]
            if ch in "({":
                depth += 1
            elif ch in ")}":
                if depth == 0:
                    pairs.append((m.group(1), " ".join(table[start:j].split())))
                    k = j + 1
                    break
                depth -= 1
    if len(pairs) < 20:
        raise ThemeError(f"src/Theme.cpp: token table read {len(pairs)} entries, expected 20 or more")
    return locals_, pairs


def read_mono_families(theme_cpp: str) -> list[str]:
    body = function_body(theme_cpp, "QString monoFamily(")
    names = re.findall(r'QStringLiteral\("([^"]+)"\)', body)
    if not names:
        raise ThemeError("src/Theme.cpp: monoFamily() lists no families")
    return names


def read_selected_row(pane_h: str) -> tuple[str, str | None, int | None]:
    """QueueRowDelegate::paint fills a selected row with a token, perhaps lighter or darker."""
    m = re.search(r"State_Selected\)\s*painter->fillRect\(r,\s*relay::theme::(\w+)"
                  r"(?:\.(lighter|darker)\((\d+)\))?\)", pane_h)
    if not m:
        raise ThemeError("src/Pane.h: QueueRowDelegate no longer fills a selected row with "
                         "`relay::theme::Token[.lighter(n)]`; teach scripts/gen-web-theme.py the new form")
    return m.group(1), m.group(2), int(m.group(3)) if m.group(3) else None


# --- a small evaluator for the token expressions -------------------------------------------------

TOKEN_RE = re.compile(r'\s*(?:(QStringLiteral\("[^"]*"\))|(\d+\.\d+|\d+)|(\w+)|(.))')


class Expr:
    """Evaluates the C++ in the token table: calls, `.lighter()`, literals, names. Anything else is
    an error, so a new construct in Theme.cpp stops the generator instead of guessing."""

    def __init__(self, text: str, env: dict, spec: dict):
        self.tokens = [t for t in TOKEN_RE.findall(text) if any(t)]
        self.i, self.env, self.spec, self.text = 0, env, spec, text

    def peek(self):
        return self.tokens[self.i] if self.i < len(self.tokens) else ("", "", "", "")

    def take(self, symbol: str | None = None):
        tok = self.peek()
        if symbol is not None and tok[3] != symbol:
            raise ThemeError(f"expected {symbol!r} in {self.text!r}")
        self.i += 1
        return tok

    def parse(self):
        value = self.term()
        while self.peek()[3] == "+":
            self.take("+")
            right = self.term()
            value = f"{value}{right}"
        if self.i != len(self.tokens):
            raise ThemeError(f"could not read {self.text!r}")
        return value

    def args(self):
        self.take("(")
        out = []
        if self.peek()[3] != ")":
            out.append(self.term_plus())
            while self.peek()[3] == ",":
                self.take(",")
                out.append(self.term_plus())
        self.take(")")
        return out

    def term_plus(self):
        value = self.term()
        while self.peek()[3] == "+":
            self.take("+")
            value = f"{value}{self.term()}"
        return value

    def term(self):
        lit, number, name, sym = self.take()
        if lit:
            value = lit[len('QStringLiteral("'):-2]
        elif number:
            value = float(number) if "." in number else int(number)
        elif name == "spec" and self.peek()[3] == ".":
            self.take(".")
            method = self.take()[2]
            args = self.args()
            if method != "uiColor":
                raise ThemeError(f"spec.{method}() is not known to the generator")
            value = self.spec["ui"].get(args[0], args[1] if len(args) > 1 else None)
        elif name and self.peek()[3] == "(":
            value = self.call(name, self.args())
        elif name:
            if name not in self.env:
                raise ThemeError(f"unknown name {name!r} in {self.text!r}")
            value = self.env[name]
        else:
            raise ThemeError(f"unexpected {sym!r} in {self.text!r}")
        while self.peek()[3] == ".":
            self.take(".")
            method = self.take()[2]
            args = self.args()
            if method == "lighter":
                value = value.lighter(args[0])
            elif method == "darker":
                value = value.darker(args[0])
            else:
                raise ThemeError(f".{method}() is not known to the generator")
        return value

    @staticmethod
    def call(name: str, args):
        functions = {
            "hex": lambda c: c.hex(),
            "rgba": lambda c: c.css_rgba(),
            "inkOn": ink_on,
            "blend": blend,
            "withAlpha": with_alpha,
            "monoFamily": lambda: "@MONO",
            "themeDataDir": lambda: "@ICONS",
        }
        if name not in functions:
            raise ThemeError(f"{name}() is not known to the generator")
        return functions[name](*args)


# --- one theme -----------------------------------------------------------------------------------

def load_theme(root: Path, theme_id: str, defaults: dict[str, Color], adopt_ui, adopt_syntax):
    path = root / "data" / "theme" / "themes" / f"{theme_id}.toml"
    data = tomllib.loads(path.read_text())
    ui = {key: Color.parse(value) for key, value in data.get("ui", {}).items()}
    syntax = {key: Color.parse(value) for key, value in data.get("syntax", {}).items()}
    spec = {"ui": ui, "variant": data.get("theme", {}).get("variant", "dark"),
            "name": data.get("theme", {}).get("name", theme_id),
            "terminal": data.get("terminal", {})}
    # adoptTokens(): each live token from the theme file, else its fallback (the compiled-in value,
    # or for `Shell` the accent).
    env = dict(defaults)
    for token, key, fallback in adopt_ui:
        env[token] = ui.get(key, env[fallback])
    for token, key, fallback in adopt_syntax:
        env[token] = syntax.get(key, env.get(fallback, defaults.get(token)))
    return spec, env


def theme_tokens(spec, env, locals_, table, mono_css: str) -> dict[str, str]:
    """The stylesheet's `@token` → value, computed as stylesheetFor() computes it."""
    scope = dict(env)
    for name, expression in locals_:
        scope[name] = Expr(expression, scope, spec).parse()
    out = {}
    for token, expression in table:
        value = Expr(expression, scope, spec).parse()
        if value == "@MONO":
            value = mono_css
        if isinstance(value, str) and value.startswith("@ICONS"):
            continue
        out[token] = value
    return out


def kebab(name: str) -> str:
    return re.sub(r"(?<=[a-z0-9])([A-Z])", r"-\1", name).lower()


def token_var(token: str) -> str:
    return f"--rt-{kebab(token.lstrip('@'))}"


# --- the stylesheet cascade ----------------------------------------------------------------------

def strip_comments(css: str) -> str:
    return re.sub(r"/\*.*?\*/", "", css, flags=re.S)


def parse_rules(css: str) -> list[tuple[list[str], list[tuple[str, str]]]]:
    rules = []
    for selectors, body in re.findall(r"([^{}]+)\{([^{}]*)\}", strip_comments(css)):
        decls = []
        for part in body.split(";"):
            if ":" in part:
                prop, value = part.split(":", 1)
                decls.append((prop.strip(), " ".join(value.split())))
        rules.append(([s.strip() for s in selectors.split(",") if s.strip()], decls))
    return rules


COMPOUND_RE = re.compile(r'^(\*|[A-Za-z]\w*)?(?:#(\w+))?((?:\[[^\]]+\])*)((?::{1,2}[\w-]+)*)$')


def parse_compound(text: str):
    m = COMPOUND_RE.match(text)
    if not m:
        return None
    kind, ident, attrs, pseudos = m.groups()
    attrs = re.findall(r'\[(\w+)="?([^"\]]*)"?\]', attrs or "")
    pseudos = re.findall(r"(::?)([\w-]+)", pseudos or "")
    return {"type": kind, "id": ident, "attrs": attrs, "pseudos": pseudos}


def matches(compound, qt_class: str, object_name: str) -> bool:
    if compound["type"] and compound["type"] != "*" and compound["type"] not in BASES.get(qt_class, [qt_class]):
        return False
    if compound["id"] and compound["id"] != object_name:
        return False
    return True


def ancestor_class(name: str) -> str:
    for css, qt_class, object_name, _ in WIDGETS:
        if object_name == name:
            return qt_class
    return "QWidget"


def selector_state(selector: str, qt_class: str, object_name: str, ancestors: list[str]):
    """None when `selector` does not apply to the widget; else (specificity, state name). The state
    is "" for a plain rule, or e.g. "hover", "dest-agent", "item-selected"."""
    parts = selector.split()
    compounds = [parse_compound(p) for p in parts]
    if any(c is None for c in compounds) or not compounds:
        return None
    last = compounds[-1]
    if not matches(last, qt_class, object_name):
        return None
    # Descendant parts must match ancestors, in order.
    chain = list(ancestors)
    for compound in reversed(compounds[:-1]):
        while chain and not matches(compound, ancestor_class(chain[-1]), chain[-1]):
            chain.pop()
        if not chain:
            return None
        chain.pop()
    if last["type"] == "*":
        return None
    states = []
    for colon, name in last["pseudos"]:
        if colon == "::" and name != "item":
            return None   # sub-controls (::drop-down, ::menu-indicator …) have no web counterpart
        states.append(name)
    for key, value in last["attrs"]:
        states.append(f"{key}-{value}")
    ids = sum(1 for c in compounds if c["id"])
    classes = sum(len(c["attrs"]) + len(c["pseudos"]) for c in compounds)
    types = sum(1 for c in compounds if c["type"] and c["type"] != "*")
    return (ids, classes, types), "-".join(kebab(s) for s in states)


def css_value(prop: str, value: str, tokens: dict[str, str]) -> str | None:
    if "url(" in value or "qlineargradient" in value:
        return None
    value = value.replace('"@mono"', "@mono")
    for token in sorted(tokens, key=len, reverse=True):   # @onWarning before @warning
        value = value.replace(token, f"var({token_var(token)})")
    if prop == "font-size":
        m = re.fullmatch(r"([0-9.]+)pt", value)
        if m:
            return f"calc(var(--rt-pt) * {m.group(1)})"
    if "@" in value:
        raise ThemeError(f"unknown token in `{prop}: {value}`")
    return value


def widget_styles(rules, tokens) -> list[tuple[str, str]]:
    """(--qss-name, value) for every widget and every state rule that reaches it."""
    out = []
    for css_name, qt_class, object_name, ancestors in WIDGETS:
        cascade: dict[str, dict[str, tuple]] = {}
        for order, (selectors, decls) in enumerate(rules):
            for selector in selectors:
                hit = selector_state(selector, qt_class, object_name, ancestors)
                if hit is None:
                    continue
                specificity, state = hit
                slot = cascade.setdefault(state, {})
                for prop, value in decls:
                    if prop in SKIP_PROPERTIES:
                        continue
                    rank = (specificity, order)
                    if prop not in slot or slot[prop][0] <= rank:
                        slot[prop] = (rank, value)
        for state in sorted(cascade, key=lambda s: (s != "", s)):
            for prop in sorted(cascade[state]):
                value = css_value(prop, cascade[state][prop][1], tokens)
                if value is None:
                    continue
                name = f"--qss-{css_name}-{state + '-' if state else ''}{prop}"
                out.append((name, value))
    return out


# --- output --------------------------------------------------------------------------------------

def mono_stack(families: list[str]) -> str:
    # Theme.cpp takes the first family installed and otherwise the system's fixed font; a browser
    # does the first half itself, and these generic names are the second half on each platform.
    quoted = [f'"{name}"' for name in families]
    return ", ".join(quoted + ["ui-monospace", '"SF Mono"', "Menlo", "Consolas", '"Roboto Mono"',
                               "monospace"])


def generate(root: Path) -> str:
    theme_h = (root / "src" / "Theme.h").read_text()
    theme_cpp = (root / "src" / "Theme.cpp").read_text()
    pane_h = (root / "src" / "Pane.h").read_text()
    defaults = read_defaults(theme_h)
    sizes = read_text_sizes(theme_h)
    adopt_ui, adopt_syntax = read_adopt(theme_cpp)
    locals_, table = read_token_table(theme_cpp)
    mono = mono_stack(read_mono_families(theme_cpp))
    rules = parse_rules(read_stylesheet(theme_cpp))
    row_token, row_op, row_factor = read_selected_row(pane_h)

    blocks = []
    widget_block = None
    for theme_id in THEMES:
        spec, env = load_theme(root, theme_id, defaults, adopt_ui, adopt_syntax)
        tokens = theme_tokens(spec, env, locals_, table, mono)
        lines = [f"  /* {spec['name']} ({spec['variant']}) */",
                 f"  color-scheme: {'light' if spec['variant'] == 'light' else 'dark'};"]
        for token, value in tokens.items():
            if token == "@mono":
                continue
            lines.append(f"  {token_var(token)}: {value};")
        for name in sorted(env):
            lines.append(f"  --rt-token-{kebab(name)}: {env[name].hex()};")
        row = env[row_token]
        if row_op:
            row = getattr(row, row_op)(row_factor)
        lines.append(f"  --rt-queue-row-selected: {row.hex()};")
        terminal = spec["terminal"]
        for key in ("background", "foreground", "cursor"):
            if key in terminal:
                lines.append(f"  --rt-term-{key}: {Color.parse(terminal[key]).hex()};")
        for n, value in enumerate(terminal.get("palette", [])):
            lines.append(f"  --rt-ansi-{n}: {Color.parse(value).hex()};")
        selector = ".relay-pane" if theme_id == THEMES[0] else f'.relay-pane[data-theme="{theme_id}"]'
        if theme_id == THEMES[0]:
            selector = f'.relay-pane, .relay-pane[data-theme="{theme_id}"]'
        blocks.append(f"{selector} {{\n" + "\n".join(lines) + "\n}")
        if widget_block is None:
            # The widget rules name tokens, never colours, so one copy serves every theme.
            widget_block = widget_styles(rules, tokens)

    common = [f"  --rt-mono: {mono};",
              '  --rt-ui-font: system-ui, -apple-system, "Segoe UI", Roboto, "Noto Sans", sans-serif;',
              "  /* One desktop point; app/pane.css raises it on touch devices. */",
              "  --rt-pt: 0.083333rem;"]
    for name in ("BodyPt", "SecondaryPt", "FloorPt"):
        common.append(f"  --rt-{kebab(name[:-2])}-size: calc(var(--rt-pt) * {sizes[name]:g});")
    common += [f"  {name}: {value};" for name, value in widget_block]

    header = ("/* SPDX-License-Identifier: GPL-3.0-or-later */\n"
              "/* GENERATED by scripts/gen-web-theme.py from src/Theme.cpp, src/Theme.h, src/Pane.h and\n"
              f"   data/theme/themes/{{{','.join(THEMES)}}}.toml. Do not edit: change the desktop theme and\n"
              "   run the script. tests/test_web_theme.py fails while this file is stale. */\n")
    return header + "\n".join(blocks) + "\n.relay-pane {\n" + "\n".join(common) + "\n}\n"


def substituted_qss(root: Path, theme_id: str) -> str:
    """The stylesheet with this script's token values, to diff against Qt's (`--qss`)."""
    theme_h = (root / "src" / "Theme.h").read_text()
    theme_cpp = (root / "src" / "Theme.cpp").read_text()
    adopt_ui, adopt_syntax = read_adopt(theme_cpp)
    spec, env = load_theme(root, theme_id, read_defaults(theme_h), adopt_ui, adopt_syntax)
    locals_, table = read_token_table(theme_cpp)
    tokens = theme_tokens(spec, env, locals_, table, "@mono")
    css = read_stylesheet(theme_cpp)
    for token, _ in table:
        if token in tokens and token != "@mono":
            value = tokens[token]
            if value.startswith("rgba("):
                c = re.findall(r"[0-9.]+", value)
                value = f"rgba({c[0]}, {c[1]}, {c[2]}, {round(float(c[3]) * 255)})"
            css = css.replace(token, value)
    return css


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--root", type=Path, default=ROOT, help="source tree to read (default: this one)")
    parser.add_argument("--output", type=Path, help="where to write (default: <root>/app/pane-theme.css)")
    parser.add_argument("--check", action="store_true", help="exit 1 if the output is stale")
    parser.add_argument("--qss", metavar="THEME", help="print the substituted stylesheet for THEME")
    args = parser.parse_args()
    try:
        if args.qss:
            sys.stdout.write(substituted_qss(args.root, args.qss))
            return 0
        text = generate(args.root)
    except ThemeError as error:
        print(f"gen-web-theme: {error}", file=sys.stderr)
        return 2
    output = args.output or args.root / OUTPUT
    if args.check:
        current = output.read_text() if output.exists() else ""
        if current != text:
            print(f"{output} is stale: run scripts/gen-web-theme.py", file=sys.stderr)
            return 1
        return 0
    output.write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
