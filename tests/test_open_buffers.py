# SPDX-License-Identifier: AGPL-3.0-or-later
"""Files open in Relay's editor, and agent writes that go through it (card #F8R7, protocol 35).

The GUI side (FilePreview::answerBufferRequest) is faked here: a `buffer_request` event is answered
from a table, the way the pane answers it from the buffer. What is pinned is the worker's half —
a dirty buffer is what read_file shows and what an edit is worked out against, a write to an open
file is handed to the editor with the revision it was computed from, a conflict or no answer
writes nothing, and a file nobody has open is written to disk exactly as before.
"""
import hashlib
import tempfile
import threading
import unittest
from pathlib import Path

from relay_core.open_buffers import OpenBuffers, conflict_text, local_key, remote_key
from relay_core.provider import Cancelled
from relay_core.tools import NO_EDITOR_ANSWER, ToolExecutor


def sha(text: str) -> str:
    return hashlib.sha256(text.encode()).hexdigest()


class FakeEditor:
    """Answers `buffer_request` the way a FilePreview would, from `texts` (path -> buffer text)."""

    def __init__(self):
        self.texts = {}
        self.requests = []
        self.reply = None          # a fixed patch answer, or None to apply the patch
        self.silent = False        # never answer: the worker must time out
        self.buffers = None

    def emit(self, event):
        if event.get("event") != "buffer_request":
            return
        self.requests.append(event)
        if self.silent:
            return
        if event["op"] == "read":
            answer = {"ok": True, "text": self.texts[event["path"]]}
        elif self.reply is not None:
            answer = dict(self.reply)
        else:
            base = self.texts[event["path"]]
            if sha(base) != event["base_sha256"]:
                answer = {"ok": False, "error": "stale"}
            else:
                self.texts[event["path"]] = event["content"]
                answer = {"ok": True, "applied": "exact", "saved": False, "sha256": sha(event["content"])}
        self.buffers.answer({"id": event["id"], "type": "buffer_result", **answer})


class OpenBufferListTests(unittest.TestCase):
    def test_update_keeps_well_formed_entries_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            real = Path(tmp) / "a.tex"
            real.write_text("x")
            link = Path(tmp) / "link.tex"
            link.symlink_to(real)
            buffers = OpenBuffers(lambda e: None)
            kept = buffers.update([
                {"path": str(link), "sha256": sha("x"), "dirty": True},
                {"path": "relative.txt", "sha256": "0"},
                {"path": "ssh://host/srv/b.py", "sha256": "1"},
                {"path": str(real)},                       # no sha
                "not an object",
            ])
            self.assertEqual(kept, 2)
            # Looked up by the resolved path, whichever name the editor or the agent used.
            self.assertEqual(buffers.entry(local_key(real))["path"], str(link))
            self.assertTrue(buffers.entry(local_key(real))["dirty"])
            self.assertFalse(buffers.entry("ssh://host/srv/b.py")["dirty"])
            self.assertIsNone(buffers.entry(None))
            with self.assertRaises(ValueError):
                buffers.update("files")

    def test_remote_key_needs_an_absolute_path(self):
        self.assertEqual(remote_key("h", "/srv/./x/../y.py"), "ssh://h/srv/y.py")
        self.assertEqual(remote_key("h", "y.py", "/home/u"), "ssh://h/home/u/y.py")
        self.assertIsNone(remote_key("h", "y.py"))
        self.assertIsNone(remote_key("", "/y.py"))

    def test_request_times_out_is_cancelled_and_is_woken(self):
        cancel = threading.Event()
        buffers = OpenBuffers(lambda e: None, cancel=cancel, local_timeout=0.05)
        self.assertIsNone(buffers.request({"op": "read", "path": "/x"}))
        cancel.set()
        with self.assertRaises(Cancelled):
            buffers.request({"op": "read", "path": "/x"})
        cancel.clear()
        slow = OpenBuffers(lambda e: None, cancel=cancel, local_timeout=30)
        timer = threading.Timer(0.05, slow.fail_pending)
        timer.start()
        self.assertIsNone(slow.request({"op": "read", "path": "/x"}))
        timer.join()
        # A late answer to a request nobody waits for is dropped, not an error.
        self.assertEqual(slow.answer({"id": "br-1"}), {"id": "br-1", "pending": False})
        with self.assertRaises(ValueError):
            slow.answer({"ok": True})

    def test_conflict_text_shows_the_editor_lines(self):
        text = conflict_text({"path": "a.tex", "conflicts": [{"line": 3, "buffer": "mine", "agent": "yours"}]})
        self.assertIn("at line 3", text)
        self.assertIn("mine", text)
        self.assertIn("yours", text)
        self.assertIn("Read the file again", text)


class OpenBufferToolTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.file = self.root / "paper.tex"
        self.file.write_text("one\ntwo\nthree\n")
        self.editor = FakeEditor()
        self.buffers = OpenBuffers(self.editor.emit, local_timeout=0.2)
        self.editor.buffers = self.buffers
        self.tools = ToolExecutor(self.tmp.name, lambda e: None, threading.Event())
        self.tools.buffers = self.buffers
        self.tools.provenance = {"turn_id": "t1", "model": "m"}

    def tearDown(self):
        self.tmp.cleanup()

    def open(self, text, dirty):
        path = str(self.file)
        self.editor.texts[path] = text
        self.buffers.update([{"path": path, "sha256": sha(text), "dirty": dirty}])

    def run_tool(self, name, args):
        return self.tools.execute(self.tools.prepare(name, args))

    def test_a_file_nobody_has_open_is_the_disk(self):
        self.run_tool("write_file", {"path": "paper.tex", "content": "new\n"})
        self.assertEqual(self.file.read_text(), "new\n")
        self.assertEqual(self.editor.requests, [])

    def test_read_shows_unsaved_text_and_a_clean_buffer_costs_nothing(self):
        self.open("one\ntwo\nthree\n", dirty=False)
        read = self.run_tool("read_file", {"path": "paper.tex"})
        self.assertNotIn("open_buffer", read)
        self.assertEqual(self.editor.requests, [])
        self.open("one\nTYPED\nthree\n", dirty=True)
        read = self.run_tool("read_file", {"path": "paper.tex"})
        self.assertEqual(read["open_buffer"], "unsaved")
        self.assertIn("TYPED", str(read))

    def test_write_to_an_open_file_goes_through_the_editor(self):
        self.open("one\ntwo\nthree\n", dirty=False)
        result = self.run_tool("write_file", {"path": "paper.tex", "content": "one\n2\nthree\n"})
        patch = [r for r in self.editor.requests if r["op"] == "patch"][0]
        self.assertEqual(patch["base_sha256"], sha("one\ntwo\nthree\n"))
        self.assertEqual(patch["turn_id"], "t1")
        self.assertEqual(patch["model"], "m")
        self.assertEqual(result["open_buffer"], {"applied": "exact", "saved": False})
        self.assertEqual(result["written_bytes"], 0)
        # Applied to the unsaved buffer; the disk waits for the user's save.
        self.assertEqual(self.editor.texts[str(self.file)], "one\n2\nthree\n")
        self.assertEqual(self.file.read_text(), "one\ntwo\nthree\n")

    def test_edit_is_worked_out_against_unsaved_text(self):
        self.open("one\nTYPED\nthree\n", dirty=True)
        prepared = self.tools.prepare("edit_file", {"path": "paper.tex", "old_string": "three", "new_string": "3"})
        self.assertIn("unsaved edits", prepared.preview)
        self.assertEqual(prepared.buffer, str(self.file))
        self.tools.execute(prepared)
        self.assertEqual(self.editor.texts[str(self.file)], "one\nTYPED\n3\n")
        patch = [r for r in self.editor.requests if r["op"] == "patch"][0]
        self.assertEqual((patch["old_string"], patch["new_string"]), ("three", "3"))
        self.assertEqual(self.file.read_text(), "one\ntwo\nthree\n")

    def test_edit_of_text_only_in_the_buffer_needs_the_editor(self):
        self.open("one\nTYPED\nthree\n", dirty=True)
        prepared = self.tools.prepare("edit_file", {"path": "paper.tex", "old_string": "TYPED", "new_string": "x"})
        self.editor.silent = True
        with self.assertRaises(ValueError) as caught:
            self.tools.execute(prepared)
        self.assertEqual(str(caught.exception), NO_EDITOR_ANSWER)
        self.assertEqual(self.file.read_text(), "one\ntwo\nthree\n")

    def test_overlapping_edits_are_a_conflict_and_write_nothing(self):
        self.open("one\ntwo\nthree\n", dirty=False)
        self.editor.reply = {"ok": False, "error": "conflict",
                             "conflicts": [{"line": 2, "buffer": "TWO", "agent": "2"}]}
        with self.assertRaises(ValueError) as caught:
            self.run_tool("write_file", {"path": "paper.tex", "content": "one\n2\nthree\n"})
        self.assertIn("TWO", str(caught.exception))
        self.assertEqual(self.file.read_text(), "one\ntwo\nthree\n")

    def test_an_editor_that_cannot_hold_it_leaves_the_disk_write(self):
        self.open("one\ntwo\nthree\n", dirty=False)
        self.editor.reply = {"ok": False, "error": "not_open"}
        self.run_tool("write_file", {"path": "paper.tex", "content": "disk\n"})
        self.assertEqual(self.file.read_text(), "disk\n")

    def test_a_silent_editor_with_a_clean_buffer_falls_back_to_disk(self):
        self.open("one\ntwo\nthree\n", dirty=False)
        self.editor.silent = True
        self.run_tool("write_file", {"path": "paper.tex", "content": "disk\n"})
        self.assertEqual(self.file.read_text(), "disk\n")


if __name__ == "__main__":
    unittest.main()
