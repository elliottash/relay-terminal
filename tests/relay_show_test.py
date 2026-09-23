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
        self.assertEqual(cols, "60")
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

    def test_stdin_csv_creates_table_manifest(self):
        result = self.run_show("-", "--name", "numbers.csv", data=b"name,value\na,2\nb,10\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        item = self.manifest(result.stdout)
        self.assertEqual((item["kind"], item["rows"], item["columns"]), ("table", 3, 2))
        self.assertEqual(Path(item["path"]).read_bytes(), b"name,value\na,2\nb,10\n")
        self.assertEqual(result.stdout.count(b"\x1b]8;;relay-media:"), 1)

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

    def test_unknown_file_produces_no_escape(self):
        path = self.root / "notes.txt"
        path.write_text("hello")
        result = self.run_show(path)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"")


if __name__ == "__main__":
    unittest.main()
