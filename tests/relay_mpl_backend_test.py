"""Integration check for Relay's optional Matplotlib backend."""

import base64
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class RelayMatplotlibTest(unittest.TestCase):
    def test_pyplot_show_emits_a_restorable_png(self):
        try:
            import matplotlib  # noqa: F401
        except ImportError:
            self.skipTest("matplotlib is optional")
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as cache:
            env = os.environ.copy()
            env["MPLBACKEND"] = "module://relay_mpl_backend"
            env["PYTHONPATH"] = str(root / "scripts") + os.pathsep + env.get("PYTHONPATH", "")
            env["XDG_CACHE_HOME"] = cache
            code = "import matplotlib.pyplot as plt; plt.plot([1, 3, 2]); plt.show()"
            result = subprocess.run([sys.executable, "-c", code], env=env, capture_output=True,
                                    timeout=30, check=True)
            self.assertIn(b"\x1b_Ga=T,t=f,f=100,q=2;", result.stdout)
            payload = result.stdout.split(b";", 1)[1].split(b"\x1b\\", 1)[0]
            path = Path(os.fsdecode(base64.b64decode(payload)))
            self.assertTrue(path.is_file())
            self.assertTrue(path.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"))


if __name__ == "__main__":
    unittest.main()
