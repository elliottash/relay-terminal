# SPDX-License-Identifier: AGPL-3.0-or-later
"""app/pane-theme.css is generated from the desktop theme, and must not drift from it.

The web view of a pane takes every colour and widget style from app/pane-theme.css, which
scripts/gen-web-theme.py writes from src/Theme.cpp, src/Theme.h, src/Pane.h and the built-in
theme files (gen.THEMES). A change to any of those that changes the output has to regenerate the file in the
same commit, or the phone and the desktop stop looking alike; this test is what says so.
"""
import importlib.util
import re
import tomllib
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "scripts" / "gen-web-theme.py"
OUTPUT = ROOT / "app" / "pane-theme.css"


def load_generator():
    spec = importlib.util.spec_from_file_location("gen_web_theme", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def defined_properties(css: str) -> set[str]:
    return set(re.findall(r"(--[\w-]+)\s*:", css))


class WebThemeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.gen = load_generator()
        cls.fresh = cls.gen.generate(ROOT)
        cls.committed = OUTPUT.read_text() if OUTPUT.exists() else ""

    def test_committed_file_is_current(self):
        self.assertTrue(self.committed, f"{OUTPUT} is missing: run scripts/gen-web-theme.py")
        self.assertEqual(
            self.committed, self.fresh,
            "app/pane-theme.css is stale: the desktop theme changed. Run scripts/gen-web-theme.py "
            "and commit app/pane-theme.css with the theme change.")

    def test_theme_file_values_reach_the_css(self):
        """The base tokens are the theme files' own values, per theme."""
        blocks = {theme_id: self.fresh.split(f'.relay-pane[data-theme="{theme_id}"]')[1].split("\n}")[0]
                  for theme_id in self.gen.THEMES}
        for theme_id, block in blocks.items():
            ui = tomllib.loads((ROOT / "data/theme/themes" / f"{theme_id}.toml").read_text())["ui"]
            with self.subTest(theme=theme_id):
                self.assertIn(f"--rt-bg: {ui['background'].lower()};", block)
                self.assertIn(f"--rt-agent: {ui['agent'].lower()};", block)
                self.assertIn(f"--rt-shell: {ui['shell'].lower()};", block)
                self.assertIn(f"--rt-muted: {ui['text_muted'].lower()};", block)

    def test_the_default_block_is_the_desktop_default(self):
        """A browser with no data-theme shows what a fresh desktop shows: Dark Copper since the
        owner's "use dark copper by default on all builds" (2026-09-18). The bare `.relay-pane`
        selector is the one that carries it."""
        self.assertEqual(self.gen.THEMES[0], "dark-copper")
        first = next(line for line in self.fresh.splitlines() if line.startswith(".relay-pane"))
        self.assertEqual(first, '.relay-pane, .relay-pane[data-theme="dark-copper"] {')
        self.assertIn('defaultThemeId() { return QStringLiteral("dark-copper"); }',
                      (ROOT / "src/Theme.cpp").read_text())

    def test_the_app_shell_wears_the_default_theme(self):
        """app/style.css (the pairing screen, the pane list, the header) is typed by hand, so it is
        held to Dark Copper's own values here, and so are the colours the phone paints its status
        bar with before the page has drawn."""
        ui = tomllib.loads((ROOT / "data/theme/themes/dark-copper.toml").read_text())["ui"]
        term = tomllib.loads((ROOT / "data/theme/themes/dark-copper.toml").read_text())["terminal"]
        css = (ROOT / "app/style.css").read_text()
        for var, key in (("--bg", "background"), ("--panel", "surface"), ("--line", "border"),
                         ("--text", "text"), ("--muted", "text_muted"), ("--accent", "accent"),
                         ("--ok", "success"), ("--warn", "warning"), ("--bad", "error")):
            with self.subTest(var=var):
                self.assertIn(f"{var}: {ui[key].lower()};", css)
        self.assertIn(f"--term-bg: {term['background'].lower()};", css)
        self.assertIn(f"--term-fg: {term['foreground'].lower()};", css)
        # …and those two are only the fallback. Inside the pane view the terminal is a descendant
        # of `.relay-pane`, so it takes the generated theme's own terminal colours and follows a
        # theme chosen on the page; the hand-typed pair is what a terminal outside a pane gets, so
        # the fallback must be the same value.
        self.assertIn(f"--term-bg: var(--rt-term-background, {term['background'].lower()});", css)
        self.assertIn(f"--term-fg: var(--rt-term-foreground, {term['foreground'].lower()});", css)
        self.assertIn("var(--rt-term-cursor,", css)
        self.assertIn(f'"theme_color": "{ui["background"].lower()}"', (ROOT / "app/manifest.webmanifest").read_text())
        self.assertIn(f'<meta name="theme-color" content="{ui["background"].lower()}">',
                      (ROOT / "app/index.html").read_text())

    def test_the_web_terminal_paints_the_generated_palette(self):
        """app/screen.js paints the sixteen ANSI colours from the generated theme.

        The grid used to hold xterm's own sixteen, so the phone's terminal stayed xterm-red on a
        desktop whose theme said otherwise, and a theme chosen on the page (pane.js `setTheme`)
        left it behind. They are now `var(--rt-ansi-N, <xterm's>)`: themed where a theme is in
        scope, unchanged where none is.
        """
        screen = (ROOT / "app/screen.js").read_text()
        # The sixteen, and only those: 16-255 are the cube and the greys, which the escape sequence
        # fixes and no theme owns. The fallback is the xterm value the file already held.
        self.assertIn("n < 16 ? `var(--rt-ansi-${n}, ${hex})` : hex", screen)
        self.assertIn("THEMED[value & 0xff]", screen,
                      "app/screen.js still paints an indexed colour from the unthemed palette")
        # The generated file defines all sixteen for every theme it writes.
        props = defined_properties(self.fresh)
        for n in range(16):
            self.assertIn(f"--rt-ansi-{n}", props)
        for name in ("--rt-term-background", "--rt-term-foreground", "--rt-term-cursor"):
            self.assertIn(name, props)

    def test_qt_colour_arithmetic(self):
        """QColor's own arithmetic, printed by a Qt 5.15 program on 2026-09-18:

            QColor("#3ec5f0").lighter(115) -> #57d6ff   QColor("#3ec5f0").darker(200) -> #1f6378
            QColor("#8b919c").darker(150)  -> #5d6168

        lighter()/darker() work in HSV, so a channel-wise approximation drifts by a few counts;
        these are the values the widgets actually paint.
        """
        Color = self.gen.Color
        accent = Color.parse("#3ec5f0")
        self.assertEqual(accent.lighter(115).hex(), "#57d6ff")   # relay-dark accent_hover
        self.assertEqual(accent.darker(200).hex(), "#1f6378")    # relay-dark selection
        self.assertEqual(Color.parse("#8b919c").darker(150).hex(), "#5d6168")   # disabled
        self.assertEqual(self.gen.ink_on(Color.parse("#e5c07b")).hex(), "#251a07")  # @onWarning
        self.assertEqual(self.gen.blend(Color.parse("#7ec88c"), Color.parse("#0f1115"), 0.5).hex(),
                         "#476d51")                                # @successBorder

    def test_pane_widgets_are_styled(self):
        """The rules the pane view draws with are all there."""
        props = defined_properties(self.fresh)
        for name in ("--qss-toast-background", "--qss-queueTitle-color", "--qss-queueTitle-letter-spacing",
                     "--qss-queueHint-color", "--qss-queueSteer-color", "--qss-queueRunning-color",
                     "--qss-transcriptHeader-color", "--qss-composer-border-radius",
                     "--qss-stripChip-border", "--qss-stripChip-font-size", "--qss-statusPicker-padding",
                     "--rt-queue-row-selected", "--rt-mono", "--rt-pt"):
            self.assertIn(name, props)

    def test_pane_css_uses_only_generated_variables(self):
        """app/pane.css draws with the generated theme and nothing else: every var() it reads is
        defined in pane-theme.css or by pane.css itself (its own layout lengths), and it names no
        colour literally, so a theme change cannot miss it."""
        path = ROOT / "app" / "pane.css"
        if not path.exists():
            self.skipTest("app/pane.css is not in this tree")
        pane_css = path.read_text()
        body = re.sub(r"/\*.*?\*/", "", pane_css, flags=re.S)
        known = defined_properties(self.fresh) | defined_properties(body)
        for used in set(re.findall(r"var\((--[\w-]+)", body)):
            self.assertIn(used, known, f"app/pane.css reads {used}, which nothing defines")
        own = defined_properties(body) - defined_properties(self.fresh)
        for name in own:
            self.assertTrue(name.startswith("--rp-"), f"app/pane.css defines {name}; its own "
                            "properties are --rp-*, theme ones come from pane-theme.css")
        self.assertIsNone(re.search(r"#[0-9a-fA-F]{3,8}\b(?![\w-])", body),
                          "app/pane.css has a literal hex colour; use a --rt- token")
        self.assertIsNone(re.search(r"\b(rgb|rgba|hsl|hsla)\(", body),
                          "app/pane.css has a literal colour function; use a --rt- token")


if __name__ == "__main__":
    unittest.main()
