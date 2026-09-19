"""Verify (#T71W) opens a guest verifier through the pane's own Tier A / Tier B door.

Owner, 2026-09-19: "with verify, it opened the codex cli, not our wrapper". Verify was written the
hour before the harness route landed (protocol 29.4) and kept calling the terminal launch, so a
Codex verifier ran as the bare TUI while the model picker two clicks away ran it wrapped. A pane
cannot be built in a unit test, so this pins the two places in the source that decide it.
"""
import re
import unittest
from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / "src"


def block(text: str, start: str) -> str:
    """The brace-balanced block that follows `start`."""
    at = text.index(start)
    open_at = text.index("{", at)
    depth = 0
    for i in range(open_at, len(text)):
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        if depth == 0:
            return text[open_at:i + 1]
    raise AssertionError(f"unbalanced block after {start!r}")


class VerifyRunnerSource(unittest.TestCase):
    def test_the_window_hands_a_guest_verifier_to_the_panes_own_door(self):
        body = block((SRC / "RelayWindow.h").read_text(), "view->onVerifyCard =")
        self.assertIn("startGuestBoardTask(runnerId, task, card)", body)
        self.assertNotIn("launchGuest(", body, "Verify must not start the guest's TUI itself")

    def test_the_pane_prefers_the_harness_and_keeps_the_tui_as_the_fallback(self):
        body = block((SRC / "Pane.h").read_text(), "void startGuestBoardTask(")
        waits = body.index("m_presets.isEmpty()")
        fallback = body.index("!guestHarnessUsable(guest)")
        wrapped = body.index("pickGuest(guest)")
        self.assertLess(waits, fallback, "which route is not known before the presets are in")
        self.assertLess(fallback, wrapped)
        self.assertRegex(body[fallback:wrapped], r"launchGuest\(guest, \{task\}")
        self.assertIn("startBoardTask(task, cardId)", body[wrapped:])

    def test_a_staged_guest_task_is_released_when_the_presets_arrive(self):
        text = (SRC / "Pane.h").read_text()
        self.assertRegex(text, re.compile(
            r"if \(!m_pendingGuestTask\.guest\.isEmpty\(\)\) \{.*?startGuestBoardTask\(pending\.guest, "
            r"pending\.task, pending\.card\);", re.S))


if __name__ == "__main__":
    unittest.main()
