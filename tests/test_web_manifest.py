# SPDX-License-Identifier: AGPL-3.0-or-later
"""The web app's manifest and its icons (docs/REMOTE-PROTOCOL.md sections 9 and 10.7).

This is the one part of the phone client nothing else can catch. iOS delivers Web Push only to a
PWA installed on the Home Screen, it installs one only from a manifest it accepts, and it takes
the Home-Screen image from ``<link rel="apple-touch-icon">`` and nowhere else — never from the
manifest's ``icons``, and never from an SVG. Every one of those failures is silent: the app still
loads, the icon is a screenshot of the page, and the push simply never arrives.

So the checks here are the ones a phone makes: the manifest parses, ``display`` is standalone,
every ``src`` is a file that exists under ``app/``, each PNG really is the size it claims (read
from its own IHDR, not from its name), there is a maskable icon, and index.html links an opaque
PNG apple-touch-icon.

The PNGs are rendered from ``app/icon.svg`` — the same mark as the desktop app, the owner's
"one icon everywhere" — and ``docs/RELEASING.md`` has the command that makes them.
"""
from __future__ import annotations

import json
import re
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "app"
MANIFEST = APP / "manifest.webmanifest"
INDEX = APP / "index.html"

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
# PNG colour types that carry no alpha channel. 3 (palette) is excluded because a palette can
# still be made transparent by a tRNS chunk, and "opaque" is the whole point for iOS.
OPAQUE_COLOUR_TYPES = {0, 2}


def png_header(path: Path) -> tuple[int, int, int]:
    """(width, height, colour type) read from the file's own IHDR."""
    data = path.read_bytes()
    if data[:8] != PNG_MAGIC:
        raise AssertionError(f"{path} is not a PNG")
    if data[12:16] != b"IHDR":
        raise AssertionError(f"{path} has no IHDR where one must be")
    width, height = struct.unpack(">II", data[16:24])
    return width, height, data[25]


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.manifest = json.loads(MANIFEST.read_text())

    def test_display_is_standalone(self):
        # Anything else and iOS opens the Home-Screen icon in Safari, with no push and no
        # standalone display mode for the app to detect.
        self.assertEqual("standalone", self.manifest["display"])

    def test_names_and_colours(self):
        self.assertEqual("Relay", self.manifest["short_name"])
        for key in ("name", "start_url", "scope", "background_color", "theme_color"):
            self.assertIn(key, self.manifest)

    def test_every_icon_src_is_a_file_under_app(self):
        for icon in self.manifest["icons"]:
            src = icon["src"]
            self.assertTrue(src.startswith("./"), f"{src} is not relative to the app's scope")
            path = (APP / src[2:]).resolve()
            self.assertTrue(path.is_relative_to(APP), f"{src} escapes app/")
            self.assertTrue(path.is_file(), f"{src} is in the manifest but not in app/")

    def test_png_icons_are_the_size_they_claim(self):
        pngs = [i for i in self.manifest["icons"] if i.get("type") == "image/png"]
        self.assertTrue(pngs, "iOS and Android both want a PNG; the manifest has only the SVG")
        for icon in pngs:
            path = APP / icon["src"][2:]
            width, height, _ = png_header(path)
            self.assertEqual(f"{width}x{height}", icon["sizes"],
                             f"{icon['src']} is {width}x{height}, the manifest says {icon['sizes']}")

    def test_the_installable_sizes_are_there(self):
        # 192 and 512 are what Chrome requires to call the app installable; 512 is also what iOS
        # falls back to for the splash screen.
        sizes = {i["sizes"] for i in self.manifest["icons"] if i.get("type") == "image/png"
                 and "any" in i.get("purpose", "any").split()}
        self.assertIn("192x192", sizes)
        self.assertIn("512x512", sizes)

    def test_there_is_a_maskable_icon(self):
        maskable = [i for i in self.manifest["icons"] if "maskable" in i.get("purpose", "").split()]
        self.assertTrue(maskable, "no maskable icon: Android crops the square one to a circle")
        for icon in maskable:
            width, height, colour = png_header(APP / icon["src"][2:])
            self.assertEqual(width, height)
            self.assertGreaterEqual(width, 512)
            self.assertIn(colour, OPAQUE_COLOUR_TYPES,
                          "a maskable icon is cropped, so it must be opaque to its own edges")

    def test_the_svg_is_still_offered(self):
        # The desktop mark is drawn once, in app/icon.svg, and the PNGs come from it. Losing the
        # SVG would mean the PNGs are the only copy and can drift from the desktop's icon.
        self.assertTrue(any(i.get("type") == "image/svg+xml" for i in self.manifest["icons"]))
        self.assertTrue((APP / "icon.svg").is_file())


class AppleTouchIconTests(unittest.TestCase):
    def setUp(self):
        self.head = INDEX.read_text()

    def link(self) -> str:
        match = re.search(r'<link\s+rel="apple-touch-icon"[^>]*href="([^"]+)"', self.head)
        self.assertIsNotNone(
            match, "index.html has no <link rel=\"apple-touch-icon\">, so iOS puts a screenshot "
                   "of the page on the Home Screen")
        return match.group(1)

    def test_index_links_the_apple_touch_icon(self):
        href = self.link()
        self.assertTrue(href.startswith("./"))
        self.assertTrue((APP / href[2:]).is_file(), f"{href} is linked but not in app/")

    def test_it_is_an_opaque_180_png(self):
        path = APP / self.link()[2:]
        width, height, colour = png_header(path)
        # 180x180 is what iOS asks for; anything else it rescales, and a transparent one it
        # composites onto black on some versions and white on others.
        self.assertEqual((180, 180), (width, height))
        self.assertIn(colour, OPAQUE_COLOUR_TYPES, "the apple-touch-icon must be opaque")

    def test_the_home_screen_name_and_standalone_hints(self):
        self.assertRegex(self.head, r'<meta\s+name="apple-mobile-web-app-title"\s+content="Relay">')
        self.assertRegex(self.head, r'<meta\s+name="apple-mobile-web-app-capable"\s+content="yes">')

    def test_the_manifest_is_linked(self):
        self.assertIn('<link rel="manifest" href="./manifest.webmanifest">', self.head)


class SafeZoneTests(unittest.TestCase):
    """The maskable icon's mark has to survive a circular crop."""

    def setUp(self):
        try:
            from PIL import Image                        # noqa: F401
        except ImportError:                              # pragma: no cover - depends on the box
            self.skipTest("Pillow is not installed")

    def test_the_mark_stays_inside_the_safe_zone(self):
        from PIL import Image
        manifest = json.loads(MANIFEST.read_text())
        for icon in manifest["icons"]:
            if "maskable" not in icon.get("purpose", "").split():
                continue
            with Image.open(APP / icon["src"][2:]) as image:
                image = image.convert("RGB")
                size = image.width
                background = image.getpixel((0, 0))
                # A maskable icon keeps only the middle 80%; the ring outside it may be cropped
                # to any shape, so nothing but the background may be there.
                margin = int(size * 0.1)
                for x in range(0, size, 7):
                    for y in (0, margin - 1, size - margin, size - 1):
                        self.assertEqual(background, image.getpixel((x, y)),
                                         f"{icon['src']} draws at ({x}, {y}), outside the safe zone")
                        self.assertEqual(background, image.getpixel((y, x)),
                                         f"{icon['src']} draws at ({y}, {x}), outside the safe zone")


if __name__ == "__main__":
    unittest.main()
