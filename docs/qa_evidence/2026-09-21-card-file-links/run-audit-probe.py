#!/usr/bin/env python3
"""Compile and run isolated interaction probes against the existing Relay build.

Default run reproduces three outstanding findings (nonzero exit expected).
AUDIT_NATIVE_ROWS=1 runs the control without prose replacement; all cases pass.
Optional arguments are QtTest case names, e.g. copyingAfterWideCharacterSurvivesResize.
Only temporary files are written; build/ must already contain the engine libraries.
"""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import sys
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[2]
with tempfile.TemporaryDirectory(prefix="relay-link-audit-") as temporary:
    temp = Path(temporary)
    source = temp / "probe.cpp"
    shutil.copyfile(here / "audit-probe.cpp", source)
    subprocess.run(["moc", str(source), "-o", str(temp / "probe.moc")], check=True)
    flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "Qt5Widgets", "Qt5Test"], text=True))
    libraries = [root / "build/engine/librelay-terminal-engine.a",
                 root / "build/librelay-outputlinks.a", root / "build/librelay-markdown.a",
                 root / "build/engine/librelay-vterm-c.a"]
    binary = temp / "probe"
    subprocess.run(["c++", "-std=c++17", "-fPIC", "-O0", "-I" + str(root / "engine"),
                    "-I" + str(root / "src"), str(source), "-o", str(binary),
                    *map(str, libraries), "-lutil", *flags], check=True)
    env = dict(os.environ)
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    env["XDG_CONFIG_HOME"] = str(temp / "config")
    sys.exit(subprocess.run([str(binary), *sys.argv[1:]], env=env).returncode)
