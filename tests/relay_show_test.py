"""The media helper's output is a bounded terminal row with a local manifest."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from urllib.parse import unquote
import wave


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "relay-show"


class RelayShowTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.env = dict(os.environ, XDG_CACHE_HOME=str(self.root / "cache"))

    def run_show(self, *args, data=None):
        return subprocess.run([sys.executable, str(SCRIPT), *map(str, args)], input=data,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=self.env)

    def manifest(self, output):
        prefix = b"\x1b]8;;relay-media:"
        self.assertTrue(output.startswith(prefix), output)
        end = output.index(b"\x1b\\", len(prefix))
        row, rows, cols, encoded = output[len(prefix):end].decode().split("/", 3)
        self.assertEqual(row, "0")
        self.assertGreaterEqual(int(rows), 1)
        self.last_shape = (int(rows), int(cols))
        self.assertIn(int(cols), range(20, 121))
        path = Path(unquote(encoded))
        self.assertTrue(path.is_file())
        self.assertIn("⠀".encode("utf-8") + b"\x1b]8;;\x1b\\", output)
        return json.loads(path.read_text())

    def test_png_uses_kitty_file_escape(self):
        image = self.root / "photo.png"
        image.write_bytes(b"\x89PNG\r\n\x1a\n" + b"x" * 24)
        result = self.run_show(image)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.startswith(b"\x1b_Ga=T,t=f,q=2,f=100;"))
        self.assertTrue(result.stdout.endswith(b"\x1b\\\n"))

    def test_gif_uses_encoded_image_format(self):
        image = self.root / "animated.gif"
        image.write_bytes(b"GIF89a" + b"x" * 24)
        result = self.run_show(image)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.startswith(b"\x1b_Ga=T,t=f,q=2,f=100;"))

    def test_stdin_csv_creates_table_manifest(self):
        result = self.run_show("-", "--name", "numbers.csv", data=b"name,value\na,2\nb,10\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        item = self.manifest(result.stdout)
        self.assertEqual((item["kind"], item["rows"], item["columns"]), ("table", 3, 2))
        self.assertEqual(item["name"], "numbers.csv")
        self.assertEqual(Path(item["path"]).read_bytes(), b"name,value\na,2\nb,10\n")
        # Header, rule and both rows drawn inline, as wide as they are (#15G5).
        self.assertEqual(self.last_shape, (4, 20))
        self.assertEqual(result.stdout.count(b"\x1b]8;;relay-media:"), 4)

    def test_long_csv_reserves_preview_and_more_line(self):
        data = b"name,value\n" + b"".join(b"row%d,%d\n" % (n, n) for n in range(40))
        result = self.run_show("-", "--name", "long.csv", data=data)
        self.assertEqual(result.returncode, 0, result.stderr)
        item = self.manifest(result.stdout)
        self.assertEqual(item["rows"], 41)
        self.assertEqual(self.last_shape[0], 2 + 15 + 1)   # header, rule, 15 rows, "… 25 more rows"
        self.assertEqual(result.stdout.count(b"relay-media:"), 18)

    def test_wave_audio_has_duration(self):
        sound = self.root / "tone.wav"
        with wave.open(str(sound), "wb") as out:
            out.setnchannels(1)
            out.setsampwidth(2)
            out.setframerate(8000)
            out.writeframes(b"\0\0" * 8000)
        result = self.run_show(sound)
        self.assertEqual(result.returncode, 0, result.stderr)
        item = self.manifest(result.stdout)
        self.assertEqual(item["kind"], "audio")
        self.assertAlmostEqual(item["duration"], 1.0)
        self.assertLessEqual(len(item["waveform"]), 64)

    def test_remote_url_is_refused(self):
        result = self.run_show("https://example.com/chart")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"only localhost", result.stderr)
        self.assertEqual(result.stdout, b"")

    def test_html_chart_with_inline_svg_is_not_mistaken_for_an_svg_file(self):
        page = self.root / "chart.html"
        page.write_text("<!doctype html><html><body><svg></svg></body></html>")
        shot = self.root / "chart.png"
        shot.write_bytes(b"\x89PNG\r\n\x1a\n" + b"x" * 24)
        result = self.run_show(page, "--snapshot", shot)
        self.assertEqual(result.returncode, 0, result.stderr)
        item = self.manifest(result.stdout)
        self.assertEqual(item["kind"], "chart")
        self.assertEqual(item["preview"], str(shot))

    def test_m4a_ftyp_box_is_audio(self):
        sound = self.root / "clip.m4a"
        sound.write_bytes(b"\0\0\0\x18ftypM4A " + b"\0" * 24)
        result = self.run_show(sound)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.manifest(result.stdout)["kind"], "audio")

    def test_unknown_file_produces_no_escape(self):
        path = self.root / "notes.txt"
        path.write_text("hello")
        result = self.run_show(path)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"")


if __name__ == "__main__":
    unittest.main()
