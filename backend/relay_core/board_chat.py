# SPDX-License-Identifier: AGPL-3.0-or-later
"""The Board's survey, and the board a console is seeded with (protocol 19.18, 33).

This module was the helper agent: a second `Agent` on the board worker, its own FIFO queue, its
own `board_chat*` messages, its own event tagging. Card #AGNT retired all of it — owner,
2026-09-20: *"an agent interface is the prompt box… why not just make it feature equal with the
terminal agent?"* A console is now an ordinary pane worker with a context (protocol 33), so its
queue is `relay_core.queue.TurnSupervisor`'s, its persistence is `agent_context`'s, its brief is
in the system prompt and its turns are `ask`.

What is left here is what was never the chat's:

* **the roster**, the one-line-per-card listing a console about the board is seeded with, so the
  first question does not cost a `board_list` round trip;
* **the survey**, the opening turn on a **freshly created** board (owner decision, 2026-09-19):
  what `project_probe` found offline, what `board_import.propose` would turn into cards, the
  probe's `hints` as the things Relay leaves alone — narrated under a read-only turn, so nothing
  is written until the owner answers. Whether a board is fresh is `<board
  folder>/survey-state.json`, which `board_init` leaves `pending` and the first survey turn
  settles: boards created before this existed have no file and are never surveyed.
"""
from __future__ import annotations

import json
import time
from pathlib import Path

from . import agent_context
from . import board as B

#: How many cards the opening roster lists (the cleanup's cap, for the same reason).
MAX_ROSTER = 400

#: The survey's state file, beside `import-state.json` in the board folder. Absent means a board
#: from before the survey existed (or one whose survey ran): never surveyed again. Only
#: `board_init` writes `pending`.
SURVEY_FILE = "survey-state.json"

#: A tab id is a `t` and twelve hex digits today; it is hashed, never used as a path, so the
#: check is only against a protocol slip — a number, an object, a megabyte of text.
MAX_TAB = 64

#: Where a tab's console conversation is kept, and under what id (protocol 30.7). Both moved to
#: `agent_context` with #AGNT — a console's persistence is a property of its *context*, not of
#: the board — and the layout is untouched, so a tab's history from before this card is found by
#: the same name. Re-exported because the board still keys by the tab it was configured with.
helper_dir = agent_context.helper_dir
helper_session_id = agent_context.helper_session_id


def validate_tab(value) -> str:
    """The tab's persistent id from `configure` (protocol 30.7), or "" when the GUI sent none.

    Absent is not an error: a GUI from before 30.7 sends no `tab`, and its console then has no
    store — one conversation per worker, gone when the worker goes.
    """
    if value is None or value == "":
        return ""
    if not isinstance(value, str) or len(value) > MAX_TAB or not value.strip():
        raise ValueError(f"tab must be the tab's id, at most {MAX_TAB} characters.")
    return value.strip()


def survey_path(board: B.Board) -> Path:
    return board.root / SURVEY_FILE


def survey_state(board: B.Board) -> str | None:
    """`pending`, `done` or None (no file: a board that predates the survey)."""
    try:
        data = json.loads(survey_path(board).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    state = data.get("state") if isinstance(data, dict) else None
    return state if state in ("pending", "running", "done") else None


def mark_survey(board: B.Board, state: str, *, note: str = "") -> None:
    """Write the survey state. Best effort: an unwritable board folder must not break a turn."""
    payload = {"version": 1, "state": state, "changed": time.strftime("%Y-%m-%dT%H:%M:%S")}
    if note:
        payload["note"] = note[:400]
    try:
        survey_path(board).write_text(json.dumps(payload, indent=1) + "\n", encoding="utf-8")
    except OSError:
        pass


def chat_brief() -> str:
    """The Board console's brief (`board_chat_brief.md`), beside the policy.

    One reader since #AGNT: every ordinary turn has the brief in its system prompt
    (`agent_context`, `brief.key: "switchboard"`), and this is the same text by the name the
    board side has always called it.
    """
    return agent_context.brief_body("switchboard")


def roster(tools) -> list[str]:
    """One line per card, the cleanup's roster shape: the board fits in the opening prompt and
    the agent reads the cards it means to touch with `board_read`."""
    lines: list[str] = []
    for card in sorted(tools.board.cards(), key=lambda c: (B._status_order(c), c.rank or "zzzz")):
        if card.id is None:
            continue
        if len(lines) >= MAX_ROSTER:
            lines.append(f"… {MAX_ROSTER} cards listed; read the rest with board_list.")
            break
        labels = ",".join(str(l) for l in (card.front.get("labels") or [])) or "-"
        lines.append(f"#{card.id} [{card.status}] {card.title} · labels {labels} · "
                     f"{card.path.relative_to(tools.board.root)}")
    return lines


def board_seed(tools) -> str:
    """The board a Board console's **first** turn is seeded with (19.18).

    The brief itself is in the system prompt since #AGNT (`agent_context`, `brief.key`); what is
    here is the part that cannot be: the board as it is right now. It goes in front of the first
    question of a conversation and never again — every later prompt is the owner's words alone,
    the conversation being the context, which is the card sessions' seeding rule.
    """
    config = tools.board.config()
    head = [f"Board: {tools.board.root} — every card is listed below; this conversation is "
            f"about the board as a whole.",
            "Sections (board.yaml columns): " + ", ".join(str(c) for c in config.get("columns") or []),
            "Category folders (board.yaml tabs): "
            + ", ".join(f"{t.get('id')}={t.get('folder') or t.get('filter')}" for t in tools.board.tabs()),
            "",
            "--- the board today ---"]
    return "\n".join(head + roster(tools) + ["--- end of the board ---", ""])


def survey_prompt(root: Path, project: Path, probe: dict, proposals: list[dict]) -> str:
    """The survey's opening prompt: what the probe found, what an import would create, the hints.

    All of it is data the worker gathered offline; the agent's job is to present it, say what
    Relay leaves alone, and ask — never to write, which its read-only scope enforces anyway.
    """
    git = probe.get("git") or {}
    trackers = probe.get("trackers") or []
    lines = ["[Board survey]",
             f"A board was just created for {project} (board folder: {root}).",
             "This is the board's opening turn: survey what the project already tracks, offer to "
             "convert it into cards, and write nothing until the owner says yes.",
             ""]
    if trackers:
        lines.append(f"Found {probe.get('counts', {}).get('items', 0)} item(s) in "
                     f"{len(trackers)} tracker(s):")
        for finding in trackers[:20]:
            lines.append(f"- {finding.get('kind')}: {finding.get('path')} "
                         f"({finding.get('count', len(finding.get('items') or []))} item(s))"
                         + (" — truncated" if finding.get("truncated") else ""))
    else:
        lines.append("No existing tracker was found (no TODO.md, backlog/, issues list, specs…). "
                     "Say so in a sentence; an empty board is a fine answer.")
    if proposals:
        lines.append("")
        lines.append(f"An import would create {len(proposals)} card(s):")
        for item in proposals[:50]:
            lines.append(f"- {item.get('title')} — from {item.get('source', {}).get('kind')}: "
                         f"{item.get('source', {}).get('path')}")
    hints = probe.get("hints") or []
    if hints:
        lines.append("")
        lines.append("Relay deliberately leaves these alone (report them as such):")
        for hint in hints[:10]:
            lines.append(f"- {hint.get('message') or hint.get('kind') or hint}")
    primary_name = git.get("primary")
    row = next((r for r in git.get("remotes") or [] if r.get("name") == primary_name), {})
    if git.get("is_repo") and row:
        why = f" ({git.get('primary_reason')})" if git.get("primary_reason") else ""
        lines.append("")
        lines.append(f"The project is a git repository; its primary remote{why} is "
                     f"{row.get('name')}: {row.get('url')}.")
        if str(row.get("forge") or "") == "github" and row.get("owner") and row.get("repo"):
            lines.append(f"Its issues are on GitHub: https://github.com/{row['owner']}/{row['repo']}/issues "
                         "— offer to look there for an issues corpus, but only as a link for now: "
                         "two-way sync is not built, so nothing is fetched or synced without the "
                         "owner asking for it in some other way.")
    # No brief here: it is in the system prompt of the console this turn runs on (#AGNT), and
    # repeating it would be the one thing this card took `board_chat`'s prompt building apart for.
    lines += ["",
              "Present the survey in a few lines: what was found, what an import would create, "
              "what Relay leaves alone, and the GitHub link if there is one. Then ask which parts "
              "to convert. This turn writes nothing — the read-only scope refuses every write "
              "tool — and the owner's answer is the confirmation."]
    return "\n".join(lines)
