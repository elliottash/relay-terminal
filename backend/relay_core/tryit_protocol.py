# SPDX-License-Identifier: AGPL-3.0-or-later
"""Try it: stage the situation, do the mechanical pass, hand over one question (protocol 31.10).

Card #JNYN, step 3 of #YZ8G.  Codex's skeptical review of the QA plan
(`docs/research/qa-across-fields/f-codex-skeptical-review.md` section B) asked for exactly three
things, and this module is those three:

* *"One Try it action launches the pinned build and disposable fixture. If preparation fails,
  report that before requesting review."* — `try_run` starts one bounded agent turn on the
  Switchboard worker whose whole job is to stage the card's situation and open it; a turn that
  could not stage writes a thread `note` and **no** `## Try it` section, so a card never asks for
  a review of something that was never staged.
* *"Automate [the mechanical steps]. Ask the person to perform only the task whose usability or
  interpretation needs observation."* — the brief tells the agent to play every mechanical step
  itself in the real app or command and save one capture per step.
* *"Give the problem without the answer. Observe whether they find it; reveal the expected result
  afterwards."* — the section names a **sealed** `expected.md` under the evidence directory
  instead of carrying the expected result, and `try_answer` is what reveals it, after the
  person's own words are already on the thread.  The worked example this generalises
  (`docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/`) made the opposite mistake in
  its scenario 2: it told the reviewer which test was flaky and then asked whether they could
  find it, which measures following instructions.

    try_run {card}             -> tryit  started / progress… / finished {out, section_written}
    try_stop                   -> tryit  finished (state "stopped") | error
    try_answer {card, answer}  -> tryit  answered {expected_revealed, human_qa}

**Try it is the third of three steps** (owner, 2026-09-21): the tests run, an AI simulator
verifies the change, and then the person is put into a simulated environment that exercises the
issue.  Step 2 is Verify (card #WC3E), whose record names the environment it drove — a
`staged:` line pointing at `docs/qa_evidence/<date>-verify-<ID>/` with a rerunnable `stage.sh`,
beside a `simulation:` line.  When that exists, `verify_staging` finds it and this turn **reuses**
it: run the script, open the thing, write the section.  Only when there is none does Try it stage
and play the mechanical pass itself, which is what the brief's step 3 is for.

The shape is `board_cleanup`'s (`board_protocol._cleanup`), because it is the same kind of thing:
a request, a tagged agent turn, streamed progress, and a summary at the end.  What is different is
that it runs *about one card*, so its events carry the card id as well as a `run_id`, and the
turn's own events are tagged `tryit: true` so an open card's thread never shows them as a chat.

One run at a time.  A second `try_run` while one is in flight is refused with a sentence — the
GUI's Try it button says "already running" rather than queueing a second staging into the same
directory.

**The answer's thread entry is a `decision`, not a `verdict`.**  `relay_core.board.ENTRY_KINDS`
has no `verdict` kind, and adding one would ripple through the format document, the policy
appendix and `relay-board.py check` for no gain: policy rule 4 already says a judgement the user
makes is a `decision` quoting their own words in quotation marks, which is exactly what a Try it
answer is.  The entry's first line names it as the Try it verdict so the thread reads as one.
"""
from __future__ import annotations

import datetime
import os
import re
import shlex
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import Callable

from . import board as B
from .board_tools import BoardToolError, card_brief, normalize_id, section_text

#: The requests this class answers.  `board_protocol.TYPES` includes them and delegates here.
TYPES = frozenset({"try_run", "try_stop", "try_answer"})

#: `docs/qa_evidence/<date>-tryit-<ID>/`, the convention this repo already runs on: the staging
#: script, the captures, the staging notes and the sealed `expected.md` all live together, and
#: the card's `links.evidence` names the directory.
EVIDENCE_ROOT = "docs/qa_evidence"

#: Where the **verifying** session leaves the simulated environment it used (owner, 2026-09-21:
#: an app card is checked in three steps — the tests run, an AI simulator verifies it, then the
#: person is put into a simulated environment that exercises the issue).  Try it is the third
#: step: when step 2 has already staged the situation, this turn reuses that fixture instead of
#: staging and playing a second one.  The record's own `staged:` line wins; this glob is the
#: convention to fall back on (`docs/qa_evidence/<date>-verify-<ID>/stage.sh`, card #WC3E).
VERIFY_DIR_GLOB = "*-verify-{card}"
WINDOWS = os.name == "nt"
STAGE_SCRIPT = "stage.ps1" if WINDOWS else "stage.sh"
STAGING_NOTES = "staging-notes.md"
#: The two lines the verification record carries, read out of the card body wherever they sit:
#: `staged: docs/qa_evidence/2026-09-21-verify-7BM4/` and `simulation: <what the AI pass drove>`.
_STAGED_RE = re.compile(r"^[ \t>]*(?:[-*+][ \t]*)?(?:\*\*)?staged(?:\*\*)?[ \t]*:[ \t]*`?"
                        r"(?P<path>[\w./\-]+)", re.I | re.M)
_SIMULATION_RE = re.compile(r"^[ \t>]*(?:[-*+][ \t]*)?(?:\*\*)?simulation(?:\*\*)?[ \t]*:[ \t]*"
                            r"(?P<text>.+)$", re.I | re.M)

#: The section the turn writes, and the two the reveal maintains.  Both are already in
#: `board.CARD_SECTIONS`, so `relay-board.py check` does not warn about them.
SECTION = "Try it"
HUMAN_QA_SECTION = "Human QA"

#: What the sealed file is called inside the evidence directory, and the line that names it.
EXPECTED_FILE = "expected.md"
#: The marker the reveal writes, so a second `try_answer` does not paste the expected result in
#: again and a reader can see at a glance that the seal is broken.
REVEAL_PREFIX = "Expected:"

#: The first words of the note a turn that could not stage anything leaves (brief, step 7).  The
#: GUI and the tests both look for it, so it is one string here rather than three spellings.
FAILED_NOTE = "Try it could not be staged:"

MAX_ANSWER = 4000               # characters of the person's answer one request may carry
MAX_EXPECTED = 8000             # characters of expected.md pasted under the section
PROGRESS_LINE_CAP = 400         # characters of one streamed progress line
MAX_REPORT = 4000               # characters of the agent's own report the `finished` event carries


class TryItError(ValueError):
    """A refusal the caller should read: one sentence, no traceback."""


def _checked(result: dict) -> dict:
    """A board write's result, or its refusal as a sentence.

    `BoardTools.run` answers `{"error": …}` rather than raising, which is right for a model and
    wrong here: a `try_answer` whose write was refused must not report that the answer is on the
    thread.
    """
    if isinstance(result, dict) and result.get("error"):
        raise TryItError(str(result["error"]))
    return result


class _Run:
    """The Try it turn in flight: which card, where it writes, and what it has said."""

    def __init__(self, card: str, run_id: str, out: Path, rid=None):
        self.card = card
        self.run_id = run_id
        self.out = out
        self.rid = rid
        self.started = time.time()
        self.text: list[str] = []
        self.turn_id = None
        self.stopped = False
        #: What the verifying session left staged, if anything (`verify_staging`).
        self.staged: dict | None = None
        self.finished = threading.Event()


class TryItCommands:
    """`try_run`, `try_stop` and `try_answer` for one board.

    Handed its collaborators rather than finding them, exactly as `ProfileCommands` is: `need`
    answers this worker's current `BoardTools` (so a `set_board` moves Try it with it), `turns`
    is the worker's turn supervisor — Try it *is* an agent turn, which is what makes it different
    from the profile — and `emit` puts an event on the wire.
    """

    def __init__(self, need: Callable[[], object], turns, emit: Callable[[dict], None] | None = None,
                 *, clock: Callable[[], float] = time.time):
        self._need = need
        self.turns = turns
        self.emit = emit or (lambda event: None)
        self.clock = clock
        self._lock = threading.Lock()
        self._run: _Run | None = None

    # ---- dispatch --------------------------------------------------------------
    @staticmethod
    def handles(kind) -> bool:
        return kind in TYPES

    def dispatch(self, request: dict) -> bool:
        kind = request.get("type")
        if kind not in TYPES:
            return False
        card = str(request.get("card") or "")
        try:
            if kind == "try_run":
                self.start(request.get("card"), rid=request.get("id"))
            elif kind == "try_stop":
                self.stop(rid=request.get("id"))
            else:
                self.answer(request.get("card"), request.get("answer"), rid=request.get("id"))
        except (TryItError, BoardToolError, B.BoardError) as exc:
            self._emit({"state": "error", "message": str(exc)}, card=card, rid=request.get("id"))
        return True

    def running(self) -> bool:
        run = self._run
        return run is not None and not run.finished.is_set()

    def running_card(self) -> str | None:
        run = self._run
        return run.card if run is not None and not run.finished.is_set() else None

    def shutdown(self) -> None:
        """The worker is going: end the run so nothing waits for a turn that will not come."""
        if self.running():
            self._finish("stopped")

    # ---- where the evidence goes -----------------------------------------------
    def evidence_dir(self, card_id: str) -> Path:
        return evidence_dir_for(self.tools().board.repo, card_id, self.clock())

    def tools(self):
        tools = self._need()
        if tools is None:                                   # pragma: no cover - defensive
            raise TryItError("This pane has no Switchboard.")
        return tools

    # ---- try_run ---------------------------------------------------------------
    def start(self, card, *, rid=None) -> _Run:
        card_id = normalize_id(card, "card")
        tools = self.tools()
        if tools.board.card_by_id(card_id) is None:
            raise TryItError(f"There is no card #{card_id} on this board.")
        agent = getattr(self.turns, "agent", None)
        if agent is None:
            raise TryItError("Configure a provider and workspace first: Try it is an agent turn.")
        if getattr(agent, "board", None) is None:
            raise TryItError("The Switchboard agent has no board tools here (this project has no "
                             "board.yaml, or its autonomy is off).")
        with self._lock:
            if self.running():
                raise TryItError(f"Try it is already running on #{self._run.card}. Stop it before "
                                 "starting another.")
            run = _Run(card_id, f"t-{os.urandom(3).hex()}", self.evidence_dir(card_id), rid)
            self._run = run
        # Its own conversation, not the card's: the brief is the whole instruction, and a Try it
        # turn that inherited a card chat would answer the last thing somebody typed there.
        self.turns.reset()
        try:
            run.out.mkdir(parents=True, exist_ok=True)
        except OSError as exc:                              # pragma: no cover - unwritable repo
            self._run = None
            raise TryItError(f"Try it could not make its evidence directory: {exc}") from exc
        card = tools.board.card_by_id(card_id)
        staged = verify_staging(tools.board.repo, card_id, card.body if card is not None else "")
        run.staged = staged
        self._emit({"state": "started", "out": self._rel(run.out), "run_id": run.run_id,
                    # Which of the two the turn is about to do, so the notice line can say it:
                    # reusing what the verifying session staged, or staging one of its own.
                    "reusing": bool(staged and staged.get("stage")),
                    "staged": (staged or {}).get("stage") or ""}, card=card_id, rid=rid)
        try:
            self.turns.submit(tryit_prompt(tools, card_id, run.out), "now", rid, None, None)
        except Exception:
            self._finish("error")
            raise
        return run

    def stop(self, *, rid=None) -> bool:
        run = self._run
        if run is None or run.finished.is_set():
            raise TryItError("There is no Try it running to stop.")
        run.stopped = True
        cancel = getattr(self.turns, "cancel", None)
        if callable(cancel):
            try:
                cancel()
            except Exception:                               # pragma: no cover - already gone
                pass
        # The turn's own `cancelled` event ends the run through `observe`; a supervisor that
        # cannot cancel (the stub in tests, a turn that never started) ends it here instead.
        if not run.finished.is_set() and not callable(cancel):
            self._finish("stopped")
        return True

    # ---- the turn's events ------------------------------------------------------
    def observe(self, event: dict) -> dict:
        """Tag a Try it turn's events and stream its progress.  Called before every emit.

        The same two jobs `board_protocol._observe_cleanup` does: the turn's events are tagged
        so the pane draws them on the board rather than in the open card's thread, and the
        readable ones are turned into `tryit` progress lines the notice area can show.
        """
        run = self._run
        if run is None or run.finished.is_set():
            return event
        name = event.get("event")
        if name == "status" and run.turn_id is None:
            run.turn_id = event.get("turn_id") or run.turn_id
        if name in ("delta", "answer") and isinstance(event.get("text"), str):
            run.text.append(event["text"])
        line = self._progress_line(event)
        if line:
            self._emit({"state": "progress", "line": line[:PROGRESS_LINE_CAP],
                        "run_id": run.run_id}, card=run.card, rid=run.rid)
        if name in ("delta", "answer", "done", "error", "cancelled", "turn_summary", "thinking",
                    "thinking_done", "tool_started", "tool_result", "status"):
            event = {**event, "tryit": True, "run_id": run.run_id, "card_id": run.card}
        if name in ("done", "error", "cancelled"):
            self._finish({"done": "done", "cancelled": "stopped"}.get(name, name))
        return event

    @staticmethod
    def _progress_line(event: dict) -> str:
        """One readable line for the notice area, or "" for an event that says nothing."""
        name = event.get("event")
        if name == "status":
            return str(event.get("text") or "").strip()
        if name == "tool_started":
            tool = str(event.get("tool") or event.get("name") or "").strip()
            return f"running {tool}" if tool else ""
        if name == "tool_result":
            tool = str(event.get("tool") or event.get("name") or "").strip()
            return f"{tool} done" if tool else ""
        if name == "error":
            return f"failed: {str(event.get('text') or '').strip()}"
        return ""

    def _finish(self, outcome: str) -> None:
        """End the run: say whether the section was written, and what the agent reported."""
        run = self._run
        if run is None or run.finished.is_set():
            return
        run.finished.set()
        self._run = None
        section, failed = "", ""
        try:
            card = self.tools().board.card_by_id(run.card)
            if card is not None:
                section = section_text(card.body, SECTION).strip()
                failed = self._failed_note(run.card)
        except (B.BoardError, OSError, TryItError):         # pragma: no cover - unreadable tree
            pass
        report = "".join(run.text).strip()[:MAX_REPORT]
        # A turn that ended badly and left neither a section nor a note of its own: the brief
        # asks the *agent* for that note (step 7), and an agent whose provider stalled, whose
        # turn was cancelled or whose budget ran out never reaches it. The worker knows the turn
        # ended, so it writes the note instead — otherwise the card carries no trace that Try it
        # was attempted at all, which is the one thing this feature exists to prevent.
        if outcome != "done" and not section and not failed:
            failed = self._write_failure_note(run, outcome)
        self._emit({"state": "stopped" if outcome == "stopped" else
                    ("error" if outcome == "error" else "finished"),
                    "outcome": outcome,
                    "out": self._rel(run.out),
                    "run_id": run.run_id,
                    "section_written": bool(section),
                    "staging_failed": bool(failed),
                    "reusing": bool(run.staged and run.staged.get("stage")),
                    "message": failed or report}, card=run.card, rid=run.rid)

    def _write_failure_note(self, run: "_Run", outcome: str) -> str:
        """Leave the brief's own sentence on the thread when the turn could not.

        Same first words as the agent's note (`FAILED_NOTE`), so a reader, the pane and
        `_failed_note` all recognise one shape however the run ended.
        """
        minutes = max(1, int((self.clock() - run.started) // 60))
        why = {"stopped": "it was stopped", "cancelled": "it was stopped"}.get(
            outcome, "the agent's turn ended with an error")
        text = (f"{FAILED_NOTE} {why} after {minutes} minute(s), before it wrote the section. "
                f"Whatever it captured is in {self._rel(run.out)}. Nothing on this card was "
                "changed, and it is not asking to be reviewed: press Try it again.")
        try:
            result = self.tools().run("board_comment", {"id": run.card, "kind": "note",
                                                        "text": text})
        except (BoardToolError, B.BoardError, TryItError, OSError):   # pragma: no cover
            return ""
        if isinstance(result, dict) and result.get("error"):          # pragma: no cover
            return ""
        return text[:PROGRESS_LINE_CAP]

    def _failed_note(self, card_id: str) -> str:
        """The turn's own "could not be staged" note, if it left one (brief step 7)."""
        try:
            entries = self.tools().board.thread(card_id)
        except (B.BoardError, OSError):                     # pragma: no cover - unreadable thread
            return ""
        for entry in reversed(entries):
            text = (entry.text or "").strip()
            if entry.kind == "note" and text.startswith(FAILED_NOTE):
                return text[:PROGRESS_LINE_CAP]
        return ""

    # ---- try_answer -------------------------------------------------------------
    def answer(self, card, answer, *, rid=None) -> dict:
        """The person's answer: their verdict on the thread, then the seal broken, then Human QA.

        Nothing is typed twice.  `## Human QA` is built from the `## Try it` section that is
        already on the card plus the answer that has just arrived, which is the whole reason the
        section is structured (open line, task, question) rather than free prose.
        """
        card_id = normalize_id(card, "card")
        if not isinstance(answer, str) or not answer.strip():
            raise TryItError("try_answer needs `answer`: what the person saw, in their words.")
        text = answer.strip()[:MAX_ANSWER]
        tools = self.tools()
        card_obj = tools.board.card_by_id(card_id)
        if card_obj is None:
            raise TryItError(f"There is no card #{card_id} on this board.")
        section = section_text(card_obj.body, SECTION).strip()
        if not section:
            raise TryItError(f"#{card_id} has no `## {SECTION}` section to answer: press Try it "
                             "first.")
        parsed = parse_section(section)

        # 1. Their words, on the thread, before anything reveals anything. Policy rule 4: a
        #    judgement the user makes is a decision that quotes them.
        entry = [f"Try it · verdict on #{card_id}"]
        if parsed["question"]:
            entry.append(f"Q: {parsed['question']}")
        entry.append(f'A: "{text}"')
        _checked(tools.run("board_comment", {"id": card_id, "kind": "decision",
                                             "text": "\n".join(entry)}))

        # 2. The seal. `expected.md` is read from the evidence directory the section names, and
        #    appended under the section — once: a second answer must not paste it in again.
        expected, revealed = self._reveal(tools, card_id, parsed, section)

        # 3. `## Human QA`, generated from the section and the answer.
        card_obj = tools.board.card_by_id(card_id)
        human = human_qa(card_id, parse_section(section_text(card_obj.body, SECTION).strip()),
                         text, expected)
        _checked(tools.run("board_update_card",
                           {"id": card_id, "base_hash": B.file_hash(card_obj.path),
                            "replace_section": {"heading": HUMAN_QA_SECTION, "text": human}}))
        result = {"card": card_id, "expected_revealed": revealed,
                  "expected": expected, "human_qa": True}
        self._emit({"state": "answered", "expected_revealed": revealed,
                    "human_qa": True, "message": "The answer is on the thread and `## Human QA` "
                    "is written from it."}, card=card_id, rid=rid)
        return result

    def _reveal(self, tools, card_id: str, parsed: dict, section: str) -> tuple[str, bool]:
        """Read the sealed file and put it under `## Try it`.  ("", False) when there is none."""
        if already_revealed(section):
            # The seal is broken once: a second answer refreshes `## Human QA` and leaves the
            # section alone, rather than pasting the expected result in again.
            return "", False
        path = parsed.get("expected_path") or ""
        if not path:
            return "", False
        full = Path(tools.board.repo) / path
        try:
            expected = full.read_text(encoding="utf-8").strip()[:MAX_EXPECTED]
        except OSError:
            expected = ""
        if not expected:
            return "", False
        card_obj = tools.board.card_by_id(card_id)
        _checked(tools.run("board_update_card",
                           {"id": card_id, "base_hash": B.file_hash(card_obj.path),
                            "append_section": {"heading": SECTION,
                                               "text": f"\n{REVEAL_PREFIX} {expected}"}}))
        return expected, True

    # ---- events ----------------------------------------------------------------
    def _rel(self, path: Path) -> str:
        try:
            return str(Path(path).relative_to(Path(self.tools().board.repo)))
        except (ValueError, TryItError):                    # pragma: no cover - outside the repo
            return str(path)

    def _emit(self, fields: dict, *, card: str, rid=None) -> None:
        event = {"event": "tryit", "card_id": card, **fields}
        if rid is not None:
            event["id"] = rid
        self.emit(event)


# ------------------------------------------------------------------ the section, read and written

#: The sealed file's path, as the section's last line names it.  Anything that looks like a path
#: ending in `expected.md` counts, so the agent may write it as prose ("Expected result: sealed
#: in docs/…/expected.md") without the reveal losing it.
_EXPECTED_RE = re.compile(r"(?P<path>[\w./\-]*" + re.escape(EXPECTED_FILE) + r")")
_OPEN_RE = re.compile(r"^\s*(?:[-*+]\s*)?(?:\d+[.)]\s*)?(?:\*\*)?(?P<label>open|run|start)?"
                      r"(?:\*\*)?\s*[:\-—]?\s*(?P<body>.+)$", re.I)
#: A line's one command or path, when the agent wrapped it in backticks — which the brief's
#: examples all do.
_CODE_RE = re.compile(r"`([^`]+)`")


def already_revealed(section: str) -> bool:
    """Whether the seal on this `## Try it` has been broken already.

    An `Expected:` line that names `expected.md` is the *pointer* the turn wrote; one that does
    not is the expected result itself, pasted under the section by an earlier answer.
    """
    for line in section.splitlines():
        stripped = line.strip()
        if stripped.startswith(REVEAL_PREFIX) and EXPECTED_FILE not in stripped:
            return True
    return False


def parse_section(section: str) -> dict:
    """`## Try it` as its three parts, for the GUI's button and for `## Human QA`.

    The section is written by a model, so this reads *shape*, not a grammar: the first line that
    carries a command, a path or a `relay://` link is how to open it; the last line that ends in
    a question mark is the question; what is between them is the task.  A section none of that
    matches still gives back its own text as the task, so nothing is lost.
    """
    lines = [line.rstrip() for line in section.splitlines()]
    out = {"open": "", "open_kind": "", "task": "", "question": "", "expected_path": "",
           "revealed": ""}
    body: list[str] = []
    for line in lines:
        stripped = line.strip()
        if not stripped:
            body.append("")
            continue
        # The sealed file's own line and the reveal written under it both begin `Expected:`, so
        # the path is what tells them apart: a line naming `expected.md` is the seal, anything
        # else beginning `Expected:` is the answer that broke it.
        match = _EXPECTED_RE.search(stripped)
        if match and not out["expected_path"]:
            out["expected_path"] = match.group("path")
            continue
        if stripped.startswith(REVEAL_PREFIX):
            out["revealed"] = stripped[len(REVEAL_PREFIX):].strip()
            continue
        if not out["open"]:
            opened = _open_line(stripped)
            if opened:
                out["open"], out["open_kind"] = opened
                continue
        if stripped.endswith("?"):
            out["question"] = _plain(stripped)
            continue
        body.append(_plain(stripped))
    out["task"] = "\n".join(body).strip()
    if not out["question"] and out["task"]:
        # A section whose question did not end in a question mark: the last sentence is it.
        tail = out["task"].splitlines()[-1].strip()
        if tail and tail != out["task"]:
            out["question"] = tail
    return out


def _open_line(line: str) -> tuple[str, str] | None:
    """`(what to open, "command" | "link" | "path")`, or None when this line opens nothing."""
    code = _CODE_RE.search(line)
    candidate = code.group(1).strip() if code else ""
    if not candidate:
        match = _OPEN_RE.match(line)
        if match is None or not match.group("label"):
            return None
        candidate = _plain(match.group("body")).strip()
    if not candidate:
        return None
    if candidate.startswith("relay://"):
        return candidate, "link"
    if re.match(r"^[A-Za-z]:[\\/]", candidate) or candidate.startswith("\\\\"):
        return candidate, "path"
    if re.fullmatch(r"[\w./~\-]+", candidate) and ("/" in candidate or candidate.endswith(".sh")):
        return candidate, "path"
    if " " in candidate or candidate.endswith(".sh"):
        return candidate, "command"
    return None


def _plain(text: str) -> str:
    """One line of Markdown as words: the bullet, the numbering and the bold markers dropped."""
    out = re.sub(r"^\s*(?:[-*+]\s+|\d+[.)]\s+)", "", text)
    out = re.sub(r"\*\*(.+?)\*\*", r"\1", out)
    return out.strip()


def human_qa(card_id: str, parsed: dict, answer: str, expected: str) -> str:
    """`## Human QA`, generated from `## Try it` and the answer — never typed twice.

    In the form `docs/BOARD-FORMAT.md` 2.8 fixes, so the two mechanisms interlock: the
    question is a **numbered line** and its answer is an **indented line beginning `Answer:`**,
    which is the only answered shape `board_tools.unanswered_human_qa` recognises.  A Try it
    section with no answer yet leaves no `## Human QA` at all — this is written by `try_answer`
    and by nothing else — so the close gate is never held up by a question nobody was asked.

    Regenerated whole on every answer, so a second answer refreshes it rather than appending a
    second copy.
    """
    question = parsed.get("question") or "What did you make of it?"
    lines = [f"1. {question}",
             f"   Answer: {answer}"]
    if expected:
        # One indented block under the numbered item: every line of it, not only the first, or
        # the continuation lines fall out of the question they belong to.
        body = " ".join(part.strip() for part in expected.splitlines() if part.strip())
        lines += ["", f"   Expected: {body}"]
    where: list[str] = []
    if parsed.get("open"):
        where.append(f"opened with `{parsed['open']}`")
    if parsed.get("expected_path"):
        where.append(f"evidence in `{Path(parsed['expected_path']).parent}`")
    if where:
        lines += ["", "Generated from `## Try it` and the answer on the thread ("
                  + ", ".join(where) + "). Press Try it again, or answer again, and it is "
                  "rewritten."]
    else:
        lines += ["", "Generated from `## Try it` and the answer on the thread. Press Try it "
                  "again, or answer again, and it is rewritten."]
    return "\n".join(lines) + "\n"


def evidence_dir_for(repo, card_id: str, now: float | None = None) -> Path:
    """`<repo>/docs/qa_evidence/<date>-tryit-<ID>` — one name, so the tool and the turn agree."""
    day = datetime.date.fromtimestamp(now if now is not None else time.time()).isoformat()
    return Path(repo) / EVIDENCE_ROOT / f"{day}-tryit-{card_id}"


# ------------------------------------------------------- what step 2 already staged (#WC3E)

def verify_staging(repo, card_id: str, body: str = "") -> dict | None:
    """The simulated environment the verifying session left behind, or None.

    Two ways of finding it, in this order, because the record is the authority and the glob is
    only the convention: the card's own `staged:` line — written by Verify beside its
    `simulation:` line (docs/BOARD-FORMAT.md 2.8) — and then
    `docs/qa_evidence/<date>-verify-<ID>/`, newest date last.  A directory that is not there is
    not a staging, and a directory without a `stage.sh` is reported with an empty `stage` so the
    brief's step 2 can say why it fell through to step 3.
    """
    root = Path(repo)
    candidates: list[Path] = []
    for match in _STAGED_RE.finditer(body or ""):
        path = match.group("path").strip().rstrip("/")
        if path:
            candidates.append(root / path)
    found = sorted((root / EVIDENCE_ROOT).glob(VERIFY_DIR_GLOB.format(card=card_id)))
    candidates += [path for path in reversed(found)]
    for path in candidates:
        if not path.is_dir():
            continue
        script = path / STAGE_SCRIPT
        notes = path / STAGING_NOTES
        simulation = _SIMULATION_RE.search(body or "")
        return {"dir": _relative(root, path),
                "stage": _relative(root, script) if script.is_file() else "",
                "notes": _relative(root, notes) if notes.is_file() else "",
                "simulation": simulation.group("text").strip()[:400] if simulation else ""}
    return None


def _relative(root: Path, path: Path) -> str:
    try:
        return str(Path(path).relative_to(root))
    except ValueError:                                      # pragma: no cover - outside the repo
        return str(path)


# ------------------------------------------------------------------ the turn's prompt

def tryit_prompt(tools, card_id: str, out: Path) -> str:
    """The Try it turn's prompt: the brief, the card, and where to put what it makes.

    The brief is `relay_core/board_tryit_brief.md` — text beside `board_policy.md`, not code —
    and the head below is the only thing that changes per run: which card, which evidence
    directory, which binary, and the one run directory a fixture may be staged under.
    """
    card = tools.board.card_by_id(card_id)
    repo = Path(tools.board.repo)
    try:
        evidence = str(Path(out).relative_to(repo))
    except ValueError:                                      # pragma: no cover - outside the repo
        evidence = str(out)
    binary = _app_binary(repo)
    staged = verify_staging(repo, card_id, card.body if card is not None else "")
    head = ["[Switchboard Try it]",
            f"Card: #{card_id} — {card.title if card is not None else ''}",
            f"Card file: {card.path.relative_to(repo) if card is not None and card.path else ''}",
            f"Board: {tools.board.root}",
            f"Project: {repo}",
            f"Evidence directory (already made, put everything in it): {evidence}",
            f"Sealed expected result: {evidence}/{EXPECTED_FILE}",
            f"Staging script to write if you have to stage it yourself: {evidence}/{STAGE_SCRIPT}",
            f"Stage fixtures under: {_run_dir()}",
            f"The app's binary, unless the card names another: {binary}"
            + ("" if binary.is_file() else "  (not built here — build it, or say so and stop)"),
            f"Today: {datetime.date.today().isoformat()}",
            ""]
    # Step 2's environment, if it left one: the one line that decides whether this turn reuses a
    # fixture (brief step 2) or stages one of its own (step 3).
    if staged and staged["stage"]:
        head.insert(-1, f"ALREADY STAGED by the verifying session: run `{_stage_command(staged['stage'])}` "
                        f"and reuse it — do not stage a second fixture and do not replay the "
                        f"mechanical steps.")
        if staged["notes"]:
            head.insert(-1, f"How that environment differs from real use: {staged['notes']}")
        if staged["simulation"]:
            head.insert(-1, f"What the AI simulator drove there: {staged['simulation']}")
    elif staged:
        head.insert(-1, f"The verifying session left {staged['dir']} but no {STAGE_SCRIPT} in it: "
                        f"read what is there, then stage it yourself (step 3).")
    else:
        head.insert(-1, "No staged environment from the verifying session: stage it yourself "
                        "(step 3).")
    return "\n".join(head + [_platform_brief(), "", "--- the card ---",
                             _card_text(tools, card_id), "--- end of the card ---"])


def _stage_command(path: str) -> str:
    if WINDOWS:
        return "pwsh -NoLogo -NoProfile -File '" + path.replace("'", "''") + "'"
    interpreter = os.environ.get("RELAY_BASH") if sys.platform == "darwin" else None
    return shlex.quote(interpreter or "bash") + " " + shlex.quote(path)


def _app_binary(repo: Path) -> Path:
    if not WINDOWS and sys.platform == "darwin":
        candidates = [repo / "build" / "Relay.app" / "Contents" / "MacOS" / "relay",
                      repo / "build" / "relay.app" / "Contents" / "MacOS" / "relay",
                      repo / "build-macos" / "Relay.app" / "Contents" / "MacOS" / "relay",
                      Path(sys.executable).parents[3] / "MacOS" / "relay",
                      repo / "build" / "relay"]
        return next((path for path in candidates if path.is_file()), candidates[0])
    if not WINDOWS:
        return repo / "build" / "relay"
    candidates = [repo / "build" / "Release" / "relay.exe", repo / "build" / "relay.exe",
                  repo / "build-app" / "Release" / "relay.exe",
                  repo / "build-windows" / "Release" / "relay.exe"]
    # The private interpreter is <install>/runtime/python/python.exe.
    candidates.append(Path(sys.executable).parent.parent.parent / "bin" / "relay.exe")
    return next((path for path in candidates if path.is_file()), candidates[0])


def _platform_brief() -> str:
    brief = card_brief("tryit")
    if not WINDOWS and sys.platform == "darwin":
        brief = brief.replace("`build/relay`", "the app bundle executable named above")
        start = brief.index("For the app:\n")
        end = brief.index("For a backend behaviour", start)
        return brief[:start] + """For the app on native macOS:

- Use an isolated disposable profile: put HOME, TMPDIR, XDG_CONFIG_HOME,
  XDG_DATA_HOME and XDG_CACHE_HOME under the fixture directory and set RELAY_KEYRING=off.
  Do not touch the owner's real profile or login Keychain. Drive a separate test user/session;
  if none is available, leave the human interaction as a manual verification step.
- Use native macOS Accessibility automation or explicit mouse/key input in the test session.
  If Accessibility or Screen Recording permission is unavailable, report the blocked check;
  never claim screenshots or interactions that did not happen. Do not type into Relay's shell.
- Save one screenshot per step under the evidence directory. Run stage.sh with the private
  Bash interpreter named by RELAY_BASH, and launch the app bundle executable named above.

""" + brief[end:]
    if not WINDOWS:
        return brief
    brief = brief.replace("stage.sh", "stage.ps1").replace("`build/relay`", "the binary named above")
    brief = brief.replace("(`/tmp/claude-…`)", "(the user's temporary directory)")
    brief = brief.replace("because a socket path over 108 bytes breaks the app; disposable, because", "and disposable, because")
    start = brief.index("For the app:\n")
    end = brief.index("For a backend behaviour", start)
    brief = brief[:start] + """For the app on native Windows:

- Use an isolated disposable profile: put HOME, USERPROFILE, TEMP, TMP, XDG_CONFIG_HOME,
  XDG_DATA_HOME and XDG_CACHE_HOME under the fixture directory and set RELAY_KEYRING=off.
  Do not change the owner's real profile or identity. Use a separate test desktop/session
  when driving input; if one is unavailable, ask the user to perform the manual step.
- Use native Windows UI Automation or explicit mouse/key input on that test desktop.
  Never type into a terminal pane inside Relay during a GUI check.
- Save one screenshot per step under the evidence directory.
- Write PowerShell staging scripts. The Open line must be an explicit command:
  `pwsh -NoLogo -NoProfile -File '<path to stage.ps1>'` (double any single quote in the path).

""" + brief[end:]
    return brief


def _run_dir() -> str:
    """The short, disposable directory a fixture is staged under.

    Short because a unix socket path is capped at 108 bytes and the app makes one under
    `XDG_RUNTIME_DIR`; disposable because the person must be able to delete the fixture without
    thinking about it.  `RELAY_TRYIT_ROOT` overrides it for a machine laid out differently.
    """
    override = os.environ.get("RELAY_TRYIT_ROOT")
    if override:
        return override
    if WINDOWS:
        return str(Path(tempfile.gettempdir()) / "relay-tryit")
    runtime = os.environ.get("XDG_RUNTIME_DIR") or ""
    if runtime.startswith("/run/user/"):
        return f"/tmp/claude-{os.getuid()}/tryit"
    return str(Path("/tmp") / f"claude-{os.getuid()}" / "tryit")


def _card_text(tools, card_id: str) -> str:
    """The card as the turn reads it: its body, and the tail of its thread."""
    result = tools.run("board_read", {"id": card_id, "thread_entries": 12})
    if result.get("error"):                                 # pragma: no cover - checked by caller
        return str(result["error"])
    lines = [str(result.get("body") or "").strip(), ""]
    entries = result.get("thread") or []
    if entries:
        lines.append("--- the thread (last entries) ---")
        for entry in entries[-12:]:
            author = entry.get("author") or "?"
            kind = entry.get("kind") or "comment"
            text = str(entry.get("text") or "").strip().replace("\n", " ")[:400]
            lines.append(f"[{kind}] {author}: {text}")
    return "\n".join(lines) + "\n"
