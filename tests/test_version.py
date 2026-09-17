import re
import unittest
from pathlib import Path

from relay_core import __version__

ROOT = Path(__file__).resolve().parents[1]


class VersionTests(unittest.TestCase):
    def test_backend_version_matches_cmake_project(self):
        match = re.search(r"project\(Relay VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text())
        self.assertIsNotNone(match)
        self.assertEqual(__version__, match.group(1))
