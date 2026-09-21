#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Writes board.json and card.json beside this file: the fixtures of tests/test_board_view.py.

The shapes are the desktop bridge's real ones — docs/qa_evidence/2026-09-21-swph-board-bridge/
board-events.jsonl, which is a `board`, a `board_card`, the two forms of `board_changed` and the
rest as `src/BoardRemote.cpp` forwarded them — after remote/board_state.py: no `path`, `root`,
`workspace` or `folder` anywhere, `project` replaced by `board_name`, a row's `session` cut to its
first eight characters. An event's own `id` is the bridge's request id (`remote-N`), never a card:
the card is `card_id`.

What the real capture has one of, this has forty of, because a phone's list is about scrolling:
40 cards across every stage, three waiting on the owner, a parked card, self-closed and verified
cards, and one card with a long body holding every Markdown construct, an HTML-injection attempt
and the board's own comment markers, whose thread ends on a question with two numbered options.

    python3 tests/fixtures/board/build.py
"""
import json
from pathlib import Path

OUT = Path(__file__).resolve().parent
ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"          # the board's id alphabet (Crockford base32)


def cid(n: int) -> str:
    n = n * 7919 + 104729
    out = ""
    for _ in range(4):
        out += ALPHABET[n % 32]
        n //= 32
    return out


TITLES = [
    "The Switchboard on the phone: cards, threads and the card actions, by touch",
    "Voice transcription mode (microphone button, hold Right Alt)",
    "Pairing by typing the code the desktop shows",
    "Changing models during rate limit retries",
    "A security section in Options",
    "Usage limits in the picker and an exhausted subscription",
    "Scrollback survives a resume",
    "Guest agents: claude and codex picked in the model box",
    "Equalize key and drag hint",
    "Idle pane recaps",
]
PLAN = [("inbox", 5), ("discussing", 5), ("planning", 2), ("planned", 4), ("ready", 2), ("executing", 4),
        ("needs-verification", 3), ("needs-qa-llm", 2), ("needs-qa-human", 2), ("deferred", 2),
        ("done", 7), ("dropped", 2)]
WORK = ["inbox", "discussing", "planning", "planned", "ready", "executing", "in-progress",
        "needs-verification", "needs-review", "needs-labels", "needs-ab", "needs-qa-llm", "needs-qa-human",
        "deferred", "done", "dropped"]

rows = []
n = 0
for status, count in PLAN:
    for k in range(count):
        n += 1
        row = {"assignee": "agent" if status in ("executing", "planning") else ("elliott" if n % 5 == 0 else None),
               "component": None, "created": f"2026-09-{10 + n % 10:02d}", "id": cid(n),
               "implemented_by": None,
               "labels": (["feature", "remote"] if n % 3 == 0 else ["bug"] if n % 4 == 0 else []),
               "milestone": "beta" if n % 2 else None, "priority": (n % 5) - 1 if n % 6 == 0 else 0,
               "private": False, "rank": f"{n:02d}a", "section": None, "session": None, "status": status,
               "tab": "bugs" if n % 4 == 0 else "features", "tasks_done": (n % 5) // 2, "tasks_total": n % 5,
               "thread_entries": n % 7, "title": f"{TITLES[n % len(TITLES)]} ({n})", "topic": None,
               "type": "work", "updated": f"2026-09-20T1{n % 10}:00:00Z", "verified_by": None,
               "waiting_on": None}
        if status == "done":
            row["implemented_by"] = "anthropic/claude-opus-5"
            # Three the agent closed itself, two another model verified, two closed by hand.
            if k < 3:
                row["verified_by"] = "anthropic/claude-opus-5"
            elif k < 5:
                row["verified_by"] = "moonshot/kimi-k3"
        if status == "executing" and k == 0:
            row["session"] = "8999d43e"
        rows.append(row)
assert len(rows) == 40

by_status: dict = {}
for row in rows:
    by_status.setdefault(row["status"], []).append(row)
# Three cards wait on the owner, in three different stages; one waits on somebody else.
by_status["discussing"][1]["waiting_on"] = "owner"
by_status["planning"][0]["waiting_on"] = "owner"
by_status["needs-qa-human"][0]["waiting_on"] = "owner"
by_status["discussing"][2]["waiting_on"] = "agent"
# A card parked by hand in a manual section, whatever its stage.
by_status["planned"][0]["section"] = "research"
QUESTION = by_status["discussing"][1]
QUESTION.update({"title": "Scrollback survives a resume", "thread_entries": 6, "tasks_total": 5, "tasks_done": 2,
                 "labels": ["feature", "remote"], "assignee": "agent", "milestone": "beta"})

CONFIG = {"all_statuses": WORK + ["active", "retired"], "autonomy": "auto",
          "column_statuses": {"inbox": ["inbox"], "discussing": ["discussing"], "planning": ["planning"],
                              "planned": ["planned"], "executing": ["executing"],
                              "needs-verification": ["needs-verification"],
                              "needs-qa": ["needs-qa-llm", "needs-qa-human"],
                              "done": ["done", "dropped"], "research": []},
          "column_titles": {"needs-qa": "Checks", "research": "Research"},
          "columns": ["inbox", "discussing", "planning", "planned", "executing", "needs-verification",
                      "needs-qa", "done", "research"],
          "labels": ["bug", "feature", "remote"],
          "statuses": {"alias": ["active", "retired"], "memory": ["active", "retired"], "work": WORK},
          # `folder` is a path key: the hub drops it, so a folder tab is an id and nothing else.
          "tabs": [{"id": "features"}, {"id": "bugs"}, {"id": "design"},
                   {"id": "deferred", "filter": "status:deferred"},
                   {"id": "done", "filter": "status:done,dropped"}]}

board = {"board_name": "relay-terminal", "cards": rows, "cards_total": len(rows), "config": CONFIG,
         "event": "board", "exists": True, "id": "remote-1", "more": False,
         "problems": [{"code": "missing_rank", "message": "no rank in front matter", "severity": "warning"}],
         "rev": 7, "state": "ready"}
(OUT / "board.json").write_text(json.dumps(board, indent=1, ensure_ascii=False) + "\n")

BODY = """# Scrollback survives a resume

## Issue

when i resume a session the terminal is empty. i want the old text back
second line of the owner's words, kept as typed <!-- relay:note hidden-marker-one -->

<!-- relay:entry 20260920T165525Z-zz author=agent kind=note -->
<script>window.pwned = 1</script>
<img src=x onerror="window.pwned = 2">
<a href="javascript:window.pwned=3">click me</a>

## Decisions (owner, 2026-09-20)

- 2026-09-20, owner: "plain text is fine" → no sqlite.
- **Bold**, *italic*, __also bold__, _also italic_, ~~struck~~, `code span with <b>tags</b>` and snake_case_name stays whole.

## Planning notes

Options considered, see [the design doc](https://relay-terminal.ai/docs/scrollback "title") and
<https://example.org/auto> and a bare https://example.com/bare. A repo link [the pane](src/Pane.h)
is not a web link, a [script link](javascript:window.pwned=4) never opens, and an image
![diagram](https://example.org/d.png) is never fetched. See also #%(other)s and \\*not emphasis\\*.

> A quote, with **bold** inside.
> Second quoted line.

1. First numbered
2. Second numbered
   - nested bullet
   - another nested
3. Third numbered

| Option | Cost | Verdict |
|---|---:|---|
| plain text | 0 | **yes** |
| sqlite | high | no \\| never |

---

```cpp
// a fence: nothing in here is Markdown, and nothing is HTML
auto x = "<div onclick='window.pwned=5'>"; <!-- kept: this is code -->
```

## Plan

### Step one

Write the file on close.

#### Deeper

Read it on resume.

## Tasks
- [x] Save on close <!-- t:a1 -->
- [x] Replay on resume <!-- t:a2 -->
- [ ] Rewind's auxiliary file <!-- t:a3 s=in-progress -->
  - [ ] Cap it at 512 KiB <!-- t:a4 blocked_by=a3 -->
- [x] ~~Compress with sqlite~~ <!-- t:a5 s=dropped -->

## QA checklist

- [ ] Resume shows the last screen
- [x] A closed pane loses nothing
""" % {"other": rows[0]["id"]}

THREAD = [
    {"attrs": {"mode": "discuss"}, "author": "owner", "entry_id": "20260920T160101Z-a1", "kind": "comment",
     "text": "where should the text live? cheap is fine"},
    {"attrs": {"model": "kimi-k3", "pane": "8999d43e", "turn": "s9f2/t-14"}, "author": "agent",
     "entry_id": "20260920T160230Z-b2", "kind": "event",
     "text": "- ✦ agent moved this card · Inbox → Discussing · first thread entry"},
    {"attrs": {"mode": "discuss", "model": "kimi-k3"}, "author": "agent", "entry_id": "20260920T160300Z-c3",
     "kind": "comment",
     "text": "Beside the session file is cheapest. <!-- t:zz hidden-marker-two -->\n\n"
             "<b onmouseover=\"window.pwned=6\">not bold</b>"},
    {"attrs": {}, "author": "agent", "entry_id": "20260920T161500Z-e5", "kind": "decision",
     "text": "Owner, 2026-09-20: \"plain text is fine\" → no sqlite."},
    {"attrs": {"pane_token": "8999d43e"}, "author": "agent", "entry_id": "20260920T163000Z-d4", "kind": "progress",
     "text": "Executing (8999d43e) · handed to a new terminal pane"},
    {"attrs": {"model": "kimi-k3", "pane": "8999d43e"}, "author": "agent", "entry_id": "20260920T165525Z-fm",
     "kind": "question",
     "text": "Two choices before this is built:\n1. Where the text lives. Recommended: beside the session file — "
             "it survives the layout's prune and is deleted with the conversation.\n2. Scope. Recommended: Relay "
             "agent sessions only, replay on resume.\n\nSay \"go\" and I take both recommendations."},
]
card = {"body": BODY, "body_truncated": False, "card_id": QUESTION["id"], "event": "board_card",
        "front": {"acceptance": "after a resume the pane shows the text it had, up to the cap, and nothing else changes",
                  "assignee": "agent", "created": "2026-09-20", "id": QUESTION["id"],
                  "labels": ["feature", "remote"], "milestone": "beta", "rank": QUESTION["rank"],
                  "status": "discussing", "type": "work", "waiting_on": "owner"},
        "hash": "4aa488effca43c70a59cf6c173ca52c858bbbf3b8b2280242c6d085453817f99", "id": "remote-2",
        "issue": "when i resume a session the terminal is empty. i want the old text back",
        "issue_heading": "Issue",
        "sections": ["Issue", "Decisions (owner, 2026-09-20)", "Planning notes", "Plan", "Tasks", "QA checklist"],
        "status": "discussing", "tab": QUESTION["tab"],
        "tasks": [
            {"blocked_by": [], "card": None, "depth": 0, "done": True, "item_id": "a1", "status": "done", "text": "Save on close"},
            {"blocked_by": [], "card": None, "depth": 0, "done": True, "item_id": "a2", "status": "done", "text": "Replay on resume"},
            {"blocked_by": [], "card": None, "depth": 0, "done": False, "item_id": "a3", "status": "in-progress", "text": "Rewind's auxiliary file"},
            {"blocked_by": ["a3"], "card": None, "depth": 1, "done": False, "item_id": "a4", "status": "open", "text": "Cap it at 512 KiB"},
            {"blocked_by": [], "card": None, "depth": 0, "done": True, "item_id": "a5", "status": "dropped", "text": "Compress with sqlite"}],
        "thread": THREAD, "thread_total": 9, "title": "Scrollback survives a resume", "type": "work"}
(OUT / "card.json").write_text(json.dumps(card, indent=1, ensure_ascii=False) + "\n")
print(f"{len(rows)} cards; the card with the question is #{QUESTION['id']}")
