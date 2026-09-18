# SPDX-License-Identifier: GPL-3.0-or-later
"""Implementer evidence for card E4TX: edit_file through the real ToolExecutor.

Run from the repository root:

    PYTHONPATH=backend python3 docs/qa_evidence/2026-09-18-edit-file-tool/transcript.py

It writes nothing outside its own temporary workspace; the output is
`implementer-transcript.txt` beside this file.
"""
import json
import tempfile
import threading
from pathlib import Path

from relay_core.tools import ToolExecutor

SOURCE = """def greet(name):
    print("hello, " + name)


def farewell(name):
    print("hello, " + name)
"""


def show(title, body):
    print(f"===== {title} =====")
    print(body)
    print()


def main():
    with tempfile.TemporaryDirectory() as root:
        path = Path(root) / "greet.py"
        path.write_text(SOURCE)
        tools = ToolExecutor(root, lambda event: None, threading.Event())

        prepared = tools.prepare("edit_file", {"path": "greet.py",
                                               "old_string": 'def farewell(name):\n    print("hello, "',
                                               "new_string": 'def farewell(name):\n    print("goodbye, "'})
        show("tool_started.preview", prepared.preview)
        show("result", json.dumps(tools.execute(prepared), indent=2))
        show("file after", path.read_text())

        for arguments in [{"old_string": "    print(", "new_string": "    log("},
                          {"old_string": "def missing(", "new_string": "x"},
                          {"old_string": "", "new_string": "x"},
                          {"old_string": "def greet", "new_string": "def greet"}]:
            try:
                tools.prepare("edit_file", {"path": "greet.py", **arguments})
            except ValueError as error:
                show(f"refused: {arguments['old_string'][:24]!r}", str(error))
        try:
            tools.prepare("edit_file", {"path": "new.py", "old_string": "a", "new_string": "b"})
        except ValueError as error:
            show("refused: file does not exist", str(error))

        prepared = tools.prepare("edit_file", {"path": "greet.py", "old_string": "    print(",
                                               "new_string": "    log(", "replace_all": True})
        show("replace_all preview", prepared.preview)
        show("replace_all result", json.dumps(tools.execute(prepared), indent=2))

        prepared = tools.prepare("edit_file", {"path": "greet.py", "old_string": "def greet", "new_string": "def hello"})
        path.write_text(SOURCE + "# the user typed a line while the model was thinking\n")
        try:
            tools.execute(prepared)
        except ValueError as error:
            show("refused at execute: the file changed", str(error))

        show("write_file result (for comparison)", json.dumps(
            tools.execute(tools.prepare("write_file", {"path": "notes.md", "content": "one\ntwo\n"})), indent=2))


main()
