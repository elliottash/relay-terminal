"""The LaTeX renderer accepts math and refuses TeX file I/O."""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


HELPER = Path(__file__).resolve().parents[1] / "scripts" / "relay-render-math"


class MathRendererTest(unittest.TestCase):
    def run_renderer(self, expression):
        with tempfile.TemporaryDirectory() as tmp:
            manifest = Path(tmp) / "math.json"
            manifest.write_text(json.dumps({"version": 1, "kind": "math", "latex": expression}))
            env = os.environ.copy()
            env["XDG_CACHE_HOME"] = tmp
            result = subprocess.run([str(HELPER), str(manifest)], env=env, capture_output=True,
                                    text=True, timeout=15)
            return result, Path(result.stdout.strip()).read_bytes() if result.returncode == 0 else b""

    def test_fraction_renders_as_png(self):
        result, png = self.run_renderer(r"\frac{a}{b} = \sqrt{x^2+y^2}")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(png.startswith(b"\x89PNG\r\n\x1a\n"))

    def test_tex_file_input_is_refused(self):
        result, _ = self.run_renderer(r"\input{/etc/passwd}")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unsupported LaTeX command", result.stderr)


if __name__ == "__main__":
    unittest.main()
