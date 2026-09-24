# SPDX-License-Identifier: AGPL-3.0-or-later
"""Board agent tools: the six `board_*` tools and their guardrails (phase 1).

Design: `docs/BOARD-DESIGN.md` sections 6.1-6.3 and the owner decisions in 12
(especially 12.3: owner text *may* be rewritten, but every change is logged in the card's
thread holding the old and the new text, so any rewrite can be reverted).

The tools sit on top of `relay_core.board`, which owns the bytes.  This module owns
*policy*: what an agent may change, how often, what has to be recorded, and how to undo
the last write.  It never calls a model and never uses the network.

Guardrails, in one place so they can be reviewed:

* **No delete tool.**  Closing a card is a move to `done` or `dropped` with a reason.  The one
  real delete is the owner's: the GUI's confirmed `board_delete` message (card #CYM9), which
  lands in `BoardTools.delete_card` below — never a tool an agent is offered — and is undoable
  for 30 s like every other write.
* **Every write appends a thread entry** naming the actor, model, pane and turn.  A change
  to owner-authored text additionally appends a `rewrite` entry carrying the old and the
  new text verbatim (decision 12.3).
* **Immutable fields:** `id`, `type`, `created`, `source`, `rank`, `status`, `private`
  are never writable through `board_update_card` (rank and status move).
* **Limits:** creates and other writes per turn, creates per hour per workspace; over the
  limit the tool returns `{"code": "board_rate_limited"}` instead of writing.
* **Atomic, hash-checked writes** through `board.Board.save`; a stale `base_hash` returns
  `{"code": "board_conflict", "current_hash": ...}` and nothing is overwritten.
* **Undo:** each write records a snapshot (file bytes plus the thread length before it),
  restorable for 30 s from the GUI toast; undoing a creation removes the file only when
  git has never seen it.
"""
from __future__ import annotations

import contextlib
import difflib
from . import filelock as fcntl
import json
import os
import re
import secrets
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable, Sequence

from . import board as B
from . import qa_verifiers as QA
from .skills import PROFILE_VERIFY_KEYS
from . import qa_policy as QP
from . import cases as CASES
from .provider import Cancelled

AUTONOMY = ("off", "suggest", "auto")

#: The folders a project may keep its board in, in precedence order (`board.BOARD_FOLDERS`):
#: `board/`, `.switchboard/`, `switchboard/`, `issues/`.
BOARD_FOLDERS = B.BOARD_FOLDERS

#: The folder a *new* board is created in: `<project>/board/board.yaml`, visible since the owner's
#: decision of 2026-09-21 (#1CXD).  A `configure` whose `board.folder` names an older spelling
#: overrides it for that pane (protocol 19.1).
BOARD_FOLDER = B.DEFAULT_BOARD_FOLDER

#: What a pane's board is, at any moment (protocol 19.12):
#:   "ready"          - the board exists; the full tools and the full policy block.
#:   "uninitialized"  - the project has no board and the GUI says one may be offered: the agent
#:                      gets `board_create_card` alone and a one-line note, and the first card
#:                      asks the user "Initialize a project and create a Board here?".
#: A pane with no board at all has no `BoardTools` and neither state.
BOARD_STATES = ("ready", "uninitialized")

#: Why the worker is asking to initialize a project (`board_init_request.reason`).
INIT_REASONS = ("agent-card", "card-command")

#: What the agent is told when there is no Board and the user has said not to make one.
#: A tool result, not an exception: the turn carries on without the board.
NO_BOARD_TEXT = ("This project has no Board and the user declined to create one. Do not "
                 "call the board tools again in this conversation; say what you would have "
                 "filed, in your reply, and carry on with the work.")

#: The one-line note an uninitialized board puts in the system prompt, in place of the policy.
UNINITIALIZED_NOTE = ("\n\nBoard: this project has no Board yet; creating a card with "
                      "board_create_card will ask the user to initialize one.\n")

#: What an uninitialized board offers: creating a card, and nothing else.  There is nothing to
#: read, move or comment on until the first card exists.
UNINITIALIZED_TOOLS = ("board_create_card",)

#: Per-turn and per-hour ceilings (design 6.3).  `board.yaml` may lower the create ceiling.
DEFAULT_LIMITS = {
    "max_creates_per_turn": 5,
    "max_writes_per_turn": 100,
    "max_creates_per_hour": 30,
}

MAX_TITLE = 200
MAX_REQUEST = 8000
MAX_TEXT = 16384
MAX_REASON = 500
MAX_SECTION = 16384
MAX_LIST_LIMIT = 50
MAX_THREAD_ENTRIES = 50
UNDO_SECONDS = 30

#: Fields no agent write may touch.  `status`, `rank` and `section` belong to `board_move_card`
#: (the last is where a card is parked, #3XZV); `source` is the provenance of the owner's own
#: words; `private` would move the file between the git tree and the private root, which is the
#: owner's decision.
#: `session` joins them (#R9G7): the pane token that holds the card is the tool's to write, from
#: the pane's own `configure`, so a model can neither invent one nor take a card by typing it.
IMMUTABLE_FIELDS = frozenset({"id", "type", "created", "source", "rank", "status", "private",
                              "section", "session"})

#: Sections an agent writes freely: the card body schema, one section per workflow stage
#: (2026-09-20, #Z4HR; `board.CARD_SECTIONS`).  Anything else in a card body is owner text: it
#: may still be rewritten (decision 12.3) but the old and new text go into the thread.
#: `Issue` is the owner's own words and stays owner text, so a rewrite of it is always logged.
#: `tests` is what proves the card, one invocation per line (#7BM4, protocol 31):
#: `tests_check` reads it, the Test suites pane links a test back to the cards that name it.
AGENT_SECTIONS = frozenset(B.CARD_SECTIONS) - {"issue"}

#: A card in a QA lane is closed with a verdict section in the body; any pane may flip it once
#: the verdict is there (owner, 2026-09-20, card #76DJ: the verdict is the gate, not the
#: closer's model family). Relay Free still may not verify (owner, 2026-09-19).
QA_STATUSES = ("needs-qa-llm", "needs-qa-human")

#: The `ToolContext.actor` of the person at the keyboard.  `board_protocol` builds the owner's half
#: of the tools with it and restores it after every message it relabels with a `author` (its
#: `_build` and `_write`), so it is what a write from the Board pane carries and an agent
#: turn never does.  Read where a write must know which side asked for it: the self-close stamp of
#: `_move` (#93WR) is the owner's hand-close and stays unstamped.
OWNER_ACTOR = "owner"

COMMENT_KINDS = ("note", "question", "decision", "evidence", "progress")

_WORD_RE = re.compile(r"[a-z0-9]+")
_QUOTE_RE = re.compile(r"[\"“”‘’']([^\"“”‘’']{3,})[\"“”‘’']")
_SECTION_RE = re.compile(r"^##[ \t]+(?P<heading>.+?)[ \t]*$", re.M)
_H1_RE = re.compile(r"^#[ \t]+(?P<title>.+?)[ \t]*$", re.M)


class BoardToolError(ValueError):
    """A refusal the model should read and correct.  Carries a machine-readable code."""

    def __init__(self, message: str, code: str = "board_refused", **extra):
        super().__init__(message)
        self.code = code
        self.extra = extra

    def to_result(self) -> dict:
        return {"error": str(self), "code": self.code, **self.extra}


def spec(name: str, description: str, properties: dict, required: list[str]) -> dict:
    return {"type": "function", "function": {"name": name, "description": description,
            "parameters": {"type": "object", "properties": properties, "required": required,
                           "additionalProperties": False}}}


_ID_ARG = {"type": "string", "description": "Card id, four characters (e.g. K7Q2); '#K7Q2' is accepted."}

#: How many tests one agent-facing `tests_run` may name (protocol 31).  The wire's own ceiling
#: is higher (`tests_protocol.MAX_IDS`): a person selecting rows in the pane knows what they
#: picked, and a model naming fifty tests is already naming more than it can read back.
MAX_AGENT_TEST_IDS = 50

#: `board_signals` (#AQ6X protocol 32).  The actions, and the two numbers the spec quotes, read
#: from `signals` so the tool's description cannot drift from the rule it describes.  Imported
#: lazily everywhere else in this module — `signals` pulls in `test_history` — but these three
#: names are needed while the specs are being built, and they are plain values.
SIGNAL_ACTIONS = ("list", "claim", "release", "dismiss", "promote")
S_AGENT_DISMISS_MAX_DAYS = 7          # signals.AGENT_DISMISS_MAX_DAYS, asserted in the tests
#: The one status a signal gates (decision 6): a card does not land while a fault its own pane's
#: runs opened is still failing.  Spelt here rather than imported from `tests_protocol`, which is
#: the tests' gate and pulls in a subprocess-running discovery module.
SIGNAL_GATE_FROM = "needs-verification"
#: How long that call waits before the run is stopped, and what a tool call may ask for.
DEFAULT_TEST_TIMEOUT = 300
MAX_TEST_TIMEOUT = 1800

TOOL_SPECS = [
    spec("board_list",
         "List Board cards: the project's own tracker, in the board folder. Exclude it from code searches; "
         "use this tool to find cards. One row per card: id, "
         "title, type, status, tab, labels, assignee, waiting_on and thread size. Search here "
         "before creating a card, so a request that already has one updates it instead.",
         {"tab": {"type": "string", "description": "Tab id from board.yaml, e.g. features, bugs, design, planning."},
          # The stage statuses this board's columns actually collect (#3XZV). The examples used to
          # be `ready`, `in-progress`, `needs-qa-llm` — legal statuses, but not ones any column of
          # a board written since #3XZV shows, so the example sent the model at empty lanes (#GMCF).
          "status": {"type": "string", "description": "Exact status, e.g. inbox, discussing, planning, "
                                                      "executing, needs-verification, done."},
          "type": {"type": "string", "enum": list(B.CARD_TYPES), "description": "work (default view), memory or alias."},
          "labels": {"type": "array", "items": {"type": "string"},
                     "description": "Every label must be present, e.g. ['bug'] for the fault list."},
          "query": {"type": "string", "description": "Case-insensitive text matched against id, title and body."},
          "limit": {"type": "integer", "minimum": 1, "maximum": MAX_LIST_LIMIT},
          # The case ledger (#95VZ): not cards but served cases, the last N rows.
          "cases": {"type": "boolean",
                    "description": "true lists the board's case ledger (cases.jsonl) instead of "
                                   "cards: the last `limit` rows, newest last, each with server, "
                                   "served_by, cost, signal and verdict. Filter with server / card."},
          "server": {"type": "string", "description": "With cases: only rows served by this skill id, "
                                                      "program path or 'person'."},
          "card": {"type": "string", "description": "With cases: only rows about this card."}},
         []),
    spec("board_read",
         "Read one card: its front matter, the Markdown body (request, decisions, tasks, QA checklist) "
         "and the tail of its thread. The returned `hash` is what board_update_card needs.",
         {"id": _ID_ARG,
          "thread_entries": {"type": "integer", "minimum": 0, "maximum": MAX_THREAD_ENTRIES,
                             "description": "How many of the newest thread entries to return (default 20)."}},
         ["id"]),
    spec("board_create_card",
         "Create a card for a request that is not finished within this turn. `request` must be the "
         "user's own words, verbatim; the title is yours. Search with board_list first: a request "
         "that already has a card updates that one. A fuzzy duplicate check can refuse the create "
         "and return possible_duplicates; repeat the call with not_duplicate_of to override it.",
         {"tab": {"type": "string", "description": "Tab id from board.yaml (features, bugs, design, marketing, planning)."},
          "status": {"type": "string", "description": "inbox for raw capture, discussing when you need an answer, planned when agreed."},
          "section": {"type": "string", "description": "Park the new card in this manual section (a "
                                                    "column that collects nothing) instead of its "
                                                    "status's own."},
          "title": {"type": "string", "description": "One line, your words; becomes the card's `# ` heading."},
          "request": {"type": "string", "description": "The user's words verbatim. Do not paraphrase or tidy them."},
          "type": {"type": "string", "enum": list(B.CARD_TYPES), "description": "work (default), memory or alias."},
          # Policy rule 12 until v5 (#GMCF): the rule is read exactly when a card is being
          # created, so it is stated here rather than on every turn's system prompt.
          "labels": {"type": "array", "items": {"type": "string"},
                     "description": "You label the card; the user never has to, and you say "
                                    "nothing about labelling in your reply. Always exactly one of "
                                    "'bug' (something built behaves wrongly) or 'feature' "
                                    "(something new or changed is asked for), decided from your "
                                    "understanding of the request, plus the obvious area labels "
                                    "('voice', 'remote', 'switchboard', ...)."},
          "source": {"type": "string", "description": "Where the request came from, e.g. 'pane 2, 2026-09-17'."},
          "related": {"type": "array", "items": {"type": "string"}, "description": "Ids of related cards."},
          "not_duplicate_of": {"type": "array", "items": {"type": "string"},
                               "description": "Ids the duplicate check flagged that you have checked and rejected."}},
         ["tab", "status", "title", "request"]),
    spec("board_update_card",
         "Change a card's front matter fields or its body sections. `base_hash` is board_read's `hash`; "
         "the write is refused if the file changed meanwhile. id, type, status, rank, created, "
         "source and private are never writable here (status and rank move; the rest are the record). "
         "`fields` carries the rest of the front matter — labels, assignee, waiting_on, milestone, "
         "component, and `priority`, an integer −1…+3 with 0 clearing the flag (#VKFV/#DPJB), and "
         "`verify`, the QA ladder's block for the card (an object: artifact, primary, also, "
         "deferred, human, criteria, sample, sign_off, effort, stakes, blast; refused naming a "
         "bad key or value). "
         # Policy rule 9 until v5 (#GMCF): a rewrite of the user's own text happens through this
         # tool and nowhere else, so the permission and the "say you did it" are stated here.
         "Rewriting text the user wrote (a request, a title, an intake note) is allowed when they "
         "ask or when it is plainly wrong: the old and the new text are recorded in the card's "
         "thread so the change can be reverted, and you say in your reply that you did it.",
         {"id": _ID_ARG,
          "base_hash": {"type": "string", "description": "The `hash` returned by board_read."},
          "fields": {"type": "object", "description": "Front matter fields to set; null removes one.",
                     "additionalProperties": True},
          "title": {"type": "string", "description": "Replace the card's `# ` heading."},
          "append_section": {"type": "object", "description": "Append text to a `## ` section, creating it if needed.",
                             "properties": {"heading": {"type": "string"}, "text": {"type": "string"}},
                             "required": ["heading", "text"], "additionalProperties": False},
          "replace_section": {"type": "object", "description": "Replace a `## ` section's body outright.",
                              "properties": {"heading": {"type": "string"}, "text": {"type": "string"}},
                              "required": ["heading", "text"], "additionalProperties": False},
          "tasks": {"type": "array", "description": "Replace the `## Tasks` checklist.",
                    "items": {"type": "object", "properties": {
                        "text": {"type": "string"},
                        "status": {"type": "string", "enum": list(B.ITEM_STATUSES)},
                        "item_id": {"type": "string", "description": "Keep an existing item's id (two characters)."},
                        "card": {"type": "string", "description": "Id of a card that mirrors this item."},
                        "blocked_by": {"type": "array", "description": "What has to happen first: the "
                                       "1-based number of another task in this list, an item id, or "
                                       "#CARD. Replaces this item's markers.",
                                       "items": {"type": ["integer", "string"]}}},
                        "required": ["text"], "additionalProperties": False}}},
         ["id", "base_hash"]),
    spec("board_move_card",
         "Move a card to another status (column), another tab (category) or another position. `reason` is "
         "required and goes into the thread. Landing work is a move to `done` (medium) or to "
         "`needs-verification` (large), from where the verifier takes it on to a QA lane or back to an "
         "earlier stage; moving into needs-qa-llm or needs-qa-human requires an evidence path. Relay "
         "stamps `implemented_by` and `verified_by` with the pane's own provider/model, so you do not "
         "type them. Closing a card that is in a QA lane requires a verdict section in the body — any "
         "pane may flip it once the verdict is there; Relay Free still may not, and the card's `qa` "
         "block still names the best verifier. The verifying session is a separate one (a different "
         "model family is the recommendation, not a rule): it writes the `## QA checklist` as its "
         "record — the revision it checked, every `## Done means` and `## Tests` line as passed / "
         "failed / missing evidence / not applicable with its evidence path, what is unresolved, and "
         "one dated line saying the review happened — and the implementer writes none. A move to "
         "`done` is refused while the card's `## Human QA` holds a numbered question with no "
         "indented `Answer:` line under it: that judgement is the user's, not an agent's. The "
         "card's `verify` block gates the same way (#1AA6): `human: required` needs an answered "
         "question (offer needs-qa-human), `sign_off` needs a line beginning `Receipt:` in "
         "`## Verdict` or `## Execution Summary`, and `deferred` holds the card out of `done` "
         "and the QA lanes until it is cleared through fields.verify.",
         {"id": _ID_ARG,
          "status": {"type": "string", "description": "Target status; the file moves into the matching state folder."},
          "section": {"type": "string", "description": "Park the card in this manual section (a column "
                                                      "that collects nothing), leaving its status "
                                                      "alone; an empty string takes it out."},
          "tab": {"type": "string", "description": "Target tab id; the file moves into that category folder."},
          "before": {"type": "string", "description": "Id of the card this one should sit before in the column."},
          "after": {"type": "string", "description": "Id of the card this one should sit after in the column."},
          "reason": {"type": "string", "description": "Why, in one line. Recorded in the thread."},
          "evidence": {"type": "string", "description": "Evidence path, e.g. docs/qa_evidence/2026-09-17-slug/."},
          "implemented_by": {"type": "string",
                             "description": "Only when Relay cannot know it (a guest CLI through "
                                            "the bridge): the model that implemented the change. "
                                            "Relay's own stamp wins."}},
         ["id", "reason"]),
    spec("board_import_items",
         "Create Board cards from tracking the project already has — a TODO.md, a backlog/ "
         "folder, an issues list, spec files — through the Board page's own import, so every "
         "card carries a `source` and is never imported twice. Only after the owner said yes: it "
         "writes. Returns what each key became.",
         {"keys": {"type": "array", "items": {"type": "string"}, "minItems": 1, "maxItems": 1000,
                    "description": "Source keys of the items to import, from `board_import_propose` "
                                   "or the survey's proposals."},
          "tab": {"type": "string", "description": "Tab id the cards land in; default features."}},
         ["keys"]),
    spec("board_comment",
         "Append one entry to a card's thread: a note, a question for the user, a decision they made, "
         "evidence, or progress. A question is numbered and carries your recommendation. A decision "
         "quotes the user's own words in quotation marks. The thread is append-only; nothing you "
         "write here is ever rewritten.",
         {"id": _ID_ARG,
          "kind": {"type": "string", "enum": list(COMMENT_KINDS)},
          "text": {"type": "string"},
          "pane_token": {"type": "string",
                         "description": "The pane's session token, when this entry records a "
                                        "hand-off to a terminal pane (Run, #HKAP): the thread "
                                        "draws it as a link that reveals that pane. At most 64 "
                                        "characters, no whitespace or '>'."}},
         ["id", "kind", "text"]),
    spec("board_claim",
         "Take a card: this terminal pane is the session working on it. One call does what the "
         "Board's Run button does — assignee agent, status executing, the card's "
         "`session` set to this pane's token, a progress entry that links back to this pane — and "
         "returns the whole card (front matter, body, tasks, recent thread), so you need no second "
         "read. Claim before you change any code, and only a card whose request is the one you are "
         "working on. A card another session holds is refused with board_claimed_elsewhere: read "
         "its thread, comment, and ask the user before you pass force.",
         {"id": _ID_ARG,
          "note": {"type": "string",
                   "description": "One or two lines on what you are about to do; appended to the "
                                  "progress entry under the claim line."},
          "force": {"type": "boolean",
                    "description": "Take a card another session holds. Only when the user says to "
                                   "take it over, or the holding session is plainly gone."}},
         ["id"]),
    spec("tests_check",
         "Check the tests a card names, without running one of them (protocol 31, #7BM4). Reads "
         "the card's `## Tests` section, what the project collects now and the stored run "
         "history, and answers only what moved: a listed test that is gone, one that has never "
         "run here, one skipped in every run, one whose source changed, one that is flaky or "
         "slow, or a card whose commits touched files no listed test is named after. It is "
         "silent when nothing is wrong. Call it before you move a card to needs-verification and "
         "fix what it names; when it names a card with no `## Tests` section, write one — one "
         "invocation per line.",
         {"card": _ID_ARG}, ["card"]),
    spec("tests_run",
         "Run named tests and wait for the verdicts: a per-test table of result and duration, "
         "with the failure message for anything that did not pass. Ids are a test's stable key, "
         "as a card's `## Tests` section or tests_check gives them — `ctest:<name>` or "
         f"`unittest:<module>.<Class>.<test>` — at most {MAX_AGENT_TEST_IDS} of them. There is "
         "no whole-suite run here: that is `scripts/test.sh` or `ctest` at the terminal, where you "
         "can watch it. One run at a time, per project.",
         {"ids": {"type": "array", "items": {"type": "string"}, "minItems": 1,
                  "maxItems": MAX_AGENT_TEST_IDS,
                  "description": "The tests to run, each `<runner>:<invocation>`."},
          "repeat_until_fail": {"type": "integer", "minimum": 0, "maximum": 100,
                                "description": "Run them up to this many times, stopping at the "
                                               "first failure — how a flaky test is caught."},
          "timeout_seconds": {"type": "integer", "minimum": 10, "maximum": 1800,
                              "description": "How long to wait before the run is stopped "
                                             "(default 300)."}},
         ["ids"]),
    spec("board_signals",
         "The faults the machine is tracking, and what you may do about one (protocol 32, "
         "#AQ6X). A *signal* is one keyed item per failing check — `ctest:<name>`, "
         "`unittest:<module.Class.test>`, `build:<target>` — opened on its second consecutive "
         "failing execution and closed only by that check passing again: you cannot mark one "
         "fixed, you run it. `list` answers with the open ones, newest to your own card first, "
         "with each one's kind, count and excerpt. `claim` says this pane is on one, exactly as "
         "board_claim takes a card, and a signal another session holds is refused with "
         "board_claimed_elsewhere. `release` gives it back — with reason `gave-up` when you could "
         "not fix it, which files it as a bug card. `dismiss` hides one you have shown is not the "
         "code's fault: `environmental` or `flaky-known` only, with a comment and an expiry of at "
         f"most {S_AGENT_DISMISS_MAX_DAYS} days, because every dismissal expires and the rest are "
         "the user's. `promote` files it as a bug card when it needs a person.",
         {"action": {"type": "string", "enum": list(SIGNAL_ACTIONS),
                     "description": "list, claim, release, dismiss or promote."},
          "key": {"type": "string",
                  "description": "The signal's key, as `list` gives it — everything but `list` "
                                 "needs one."},
          "reason": {"type": "string",
                     "description": "On `release`, why you are letting go: `gave-up` files it as "
                                    "a bug card, anything else simply frees it. On `dismiss`, "
                                    "`environmental` or `flaky-known`."},
          "comment": {"type": "string",
                      "description": "On `dismiss`, one line on what you checked and why this "
                                     "failure is not the code's fault. Required."},
          "until": {"type": "string",
                    "description": "On `dismiss`, the date it comes back (YYYY-MM-DD), at most "
                                   f"{S_AGENT_DISMISS_MAX_DAYS} days out."},
          "force": {"type": "boolean",
                    "description": "On `claim`, take a signal another session holds. Only when "
                                   "the user says to, or that session is plainly gone."}},
         ["action"]),
    spec("board_try",
         "Try it (protocol 31.10, #JNYN): prepare the card's situation for a person and hand them "
         "one task and one question. Call it as the last step of delivering a card, once it is in "
         "needs-verification. It answers with the brief the Board's Try it button runs — "
         "what the thing to open is, whether the verifying session already staged the situation "
         "(then you run its stage.sh instead of staging a second one), where the evidence goes, "
         "and the rule that the expected result is sealed in expected.md and never written on the "
         "card. Do that work in this turn and write `## Try it` with board_update_card. It is "
         "refused during a Board cleanup, which has no machine of its own to stage on.",
         {"card": _ID_ARG}, ["card"]),
    spec("board_case",
         "Log one served case to the board's case ledger (cases.jsonl) when a person did the work "
         "by hand — a referee report, a client matter, a payment — naming what served it, who, "
         "what it cost and a one-line reference to the input, never its content. The result "
         "carries third_case_hint when this is the third person-served case of that server in "
         "90 days; then you may add one line to your reply offering to build a server, and "
         "never create a card for it yourself.",
         {"server": {"type": "string",
                     "description": "What served the case: a skill id, a program path, or "
                                    "'person' for work with no server yet."},
          "served_by": {"type": "string",
                        "description": "'person' (default), or a model signature such as "
                                       "anthropic/claude-opus-5-5."},
          "cost": {"description": "What it cost: {tokens, seconds, money}, or a phrase like "
                                  "'3 h', '45 min', '$12'.",
                   "anyOf": [{"type": "string"}, {"type": "object"}]},
          "input": {"type": "string",
                    "description": "A path or a one-line reference to the input (a file, an "
                                   "id, a subject line). Never the content. Dropped when the "
                                   "loaded skill's profile says confidential: yes."},
          "card": {"type": "string", "description": "The card this case belongs to, if any."},
          "verdict": {"type": "string", "enum": list(CASES.VERDICTS),
                      "description": "pass, fail or pending (default pending)."},
          "signal": {"type": "string",
                     "description": "The verify mode that judged it (script, probe, metric, "
                                    "ai-text, ai-visual, level, pairwise, person, world)."},
          "escalated": {"type": "string",
                        "description": "Who it was escalated to, when it was."}},
         ["server"]),
]

#: The three tools a whole-board cleanup needs and an ordinary turn does not (protocol 19.9).
#: They are offered only while `board_cleanup` is running, so a pane agent's every turn does
#: not carry three more tool schemas — and cannot merge the user's cards on a whim.
CLEANUP_TOOL_SPECS = [
    spec("board_merge_cards",
         "Fold one or more redundant cards into one surviving card. Nothing is deleted: each "
         "merged card keeps its file and its id, gains a `## Resolution` naming the survivor and "
         "is closed as `dropped`, its text is copied into the survivor under `## Merged in`, and "
         "its thread is carried over. Read every card first; merge only cards that are genuinely "
         "the same request.",
         {"into": {**_ID_ARG, "description": "The card that survives and keeps the work."},
          "cards": {"type": "array", "items": {"type": "string"}, "minItems": 1, "maxItems": 10,
                    "description": "Ids of the cards folded into it."},
          "reason": {"type": "string", "description": "Why they are the same request, in one line."}},
         ["into", "cards", "reason"]),
    spec("board_split_card",
         "Split a card that mixes unrelated work into one card per piece. Each part's `request` is "
         "the user's own words for that piece, quoted verbatim from the original; the title is "
         "yours. The original stays as the record, gains a `## Split` section naming the new cards, "
         "and is closed only when `close` is true, which is right when every piece moved out.",
         {"id": _ID_ARG,
          "parts": {"type": "array", "minItems": 2, "maxItems": 10,
                    "description": "One entry per piece of work.",
                    "items": {"type": "object", "properties": {
                        "title": {"type": "string", "description": "One line, your words."},
                        "request": {"type": "string",
                                    "description": "The user's words for this piece, verbatim from the original card."},
                        "status": {"type": "string", "description": "Defaults to the original's status."},
                        "tab": {"type": "string", "description": "Defaults to the original's category folder."},
                        "labels": {"type": "array", "items": {"type": "string"}}},
                        "required": ["title", "request"], "additionalProperties": False}},
          "reason": {"type": "string", "description": "Why the card mixes unrelated work, in one line."},
          "close": {"type": "boolean",
                    "description": "Close the original as `dropped` because every piece moved out."}},
         ["id", "parts", "reason"]),
    spec("board_sections",
         "Change the board's own structure in the board's board.yaml: which sections (columns) the one "
         "list is divided into and in what order, what each of them collects, what they are called, "
         "and which category folders (tabs) a card's file can live in. Add, remove, merge and rename "
         "a section are all this one call. No card moves and no status changes: dropping a section "
         "does not hide its cards — a status no section collects gets a section of its own — but a "
         "tab whose folder still holds cards cannot be dropped. Use this sparingly: it changes the "
         "board for everyone.",
         {"columns": {"type": "array", "items": {"type": "string"},
                      "description": f"The whole ordered section list, from: {', '.join(B.COLUMN_IDS)}, "
                                     "or an id of your own that column_statuses gives statuses to."},
          "column_statuses": {"type": "object",
                              "description": "Which statuses each section collects, e.g. "
                                             "{\"needs-qa\": [\"needs-qa-llm\", \"needs-qa-human\"]}. "
                                             "Merging two sections is one section with both sets, the "
                                             "other left out of columns. One status, one section. "
                                             "An explicit empty list is a manual section, one you "
                                             "fill by hand with board_move_card's `section`."},
          "column_titles": {"type": "object",
                            "description": "What a section is called, over an id that does not change, "
                                           "e.g. {\"ready\": \"Up next\"}. \"\" puts the name back."},
          "tabs": {"type": "array", "description": "The whole tab list, each {id, folder} or {id, filter}.",
                   "items": {"type": "object", "properties": {
                       "id": {"type": "string"}, "folder": {"type": "string"},
                       "filter": {"type": "string"}},
                       "required": ["id"], "additionalProperties": False}},
          "reason": {"type": "string", "description": "Why the structure no longer fits, in one line."}},
         ["reason"]),
]

#: The tools of every turn.  The cleanup-only three are in `CLEANUP_TOOL_NAMES`; `ALL_TOOL_NAMES`
#: is what `handles` answers to, since a cleanup call still arrives through the same dispatch.
TOOL_NAMES = tuple(s["function"]["name"] for s in TOOL_SPECS)
CLEANUP_TOOL_NAMES = tuple(s["function"]["name"] for s in CLEANUP_TOOL_SPECS)

#: Cleanup-only tools the owner may also reach directly, through a control in the GUI rather than
#: through a model: the section editor behind the gear on the Board's list of sections. The
#: fence is on the agent's autonomy, and this is the owner asking for it by hand.
OWNER_TOOLS = ("board_sections",)
ALL_TOOL_NAMES = TOOL_NAMES + CLEANUP_TOOL_NAMES
WRITE_TOOLS = ("board_create_card", "board_update_card", "board_move_card", "board_comment",
               "board_claim", "board_case",
               "board_merge_cards", "board_split_card", "board_sections", "board_import_items")

#: The statuses that mean "somebody is on this" (#R9G7).  A card in one of them with a `session`
#: is *held*: `board_claim` refuses it for anybody else without `force`, and claiming a card
#: already in one of them leaves its status alone — `in-progress` is the same thing on a board
#: configured before the stage statuses (#3XZV).
CLAIMED_STATUSES = ("executing", "in-progress")

#: What a claim writes as the first line of its progress entry.  The token's first 8 characters
#: are what the GUI draws as the link, exactly as Run's `Running (xxxxxxxx) · …` does.
CLAIM_LINE = "Claimed ({short}) · working on it from a terminal pane"
CLAIM_LINE_NO_TOKEN = "Claimed · working on it from a terminal pane"

#: What `release_claims` writes as the first line of its entry when the pane goes (#R9G7): the
#: same eight characters the claim named, so a thread reads `Claimed (xxxxxxxx) …` and then
#: `Released (xxxxxxxx) …` and it is obvious which pane let go of the card.
RELEASE_LINE = "Released ({short}) · {reason}"

#: Said in the result when this worker has no pane token: the Board worker, a test, or a
#: GUI too old to send one.  The claim still happens; it just cannot be linked to a pane.
NO_TOKEN_NOTE = ("This worker has no pane session token, so the card records no `session` and the "
                 "entry carries no pane link. The claim itself stands.")


# ------------------------------------------------------------------ small helpers

def check_pane_token(value, what: str = "pane_token") -> str | None:
    """A pane session token, or None.  The one rule both `board_comment` and `configure` use.

    The token persists as a thread entry attribute, inside an HTML comment: whitespace would
    split the attribute and `>` would close the comment early, so both are refused rather than
    quietly rewritten, and 64 characters is the cap the protocol sets (19.10, 19.19).
    """
    if value is None:
        return None
    if not isinstance(value, str):
        raise BoardToolError(f"{what} must be a string.")
    token = value.strip()
    if not token:
        return None
    if len(token) > 64 or re.search(r"[\s>]", token):
        raise BoardToolError(f"{what} is at most 64 characters, with no whitespace or '>'.")
    return token


def normalize_id(value, what: str = "id") -> str:
    if not isinstance(value, str):
        raise BoardToolError(f"{what} must be a card id such as K7Q2.")
    text = value.strip().lstrip("#").upper()
    if not B.valid_id(text):
        raise BoardToolError(f"{value!r} is not a card id: four Crockford-base32 characters with a letter, e.g. K7Q2.")
    return text


def model_family(model: str | None) -> str:
    """The vendor family of a model string, for the `qa` recommendation block.

    One line since card #T71W: `relay_core.qa_verifiers.family` is the single table, so the
    signature form and the free-text form of the same model land on the same family
    (`anthropic/claude-opus-5-5` and `Claude Opus 5.5 (pane 2)` are both `anthropic`) and an
    aggregator's route is read as the model's vendor (`openrouter/deepseek-…` is `deepseek`).
    Before that this split on the slash and took the first word, so `Claude Opus 5.5` was
    `claude` and `anthropic/claude-opus-5-5` was `anthropic` — two names for one lab, and the
    independence rule let each close what the other wrote.
    """
    return QA.family(model)


def _words(text: str) -> set[str]:
    return {w for w in _WORD_RE.findall((text or "").lower()) if len(w) > 2}


def similarity(a: str, b: str) -> float:
    """How alike two card titles/requests are.

    Half a character-level ratio, half a word-overlap ratio.  Characters alone call two
    requests that share their boilerplate ("please add a ... to Relay") duplicates; words
    alone miss a reworded title.  Averaging them keeps an exact repeat at 1.0 while
    boilerplate on its own stays well under the threshold.
    """
    left, right = (a or "").strip().lower(), (b or "").strip().lower()
    if not left or not right:
        return 0.0
    chars = difflib.SequenceMatcher(None, left, right).ratio()
    wa, wb = _words(left), _words(right)
    overlap = len(wa & wb) / len(wa | wb) if wa and wb else chars
    return (chars + overlap) / 2


def section_headings(body: str) -> list[str]:
    return [m.group("heading").strip() for m in _SECTION_RE.finditer(body or "")]


def _heading_matches(found: str, wanted: str) -> bool:
    """One `## ` heading names the same section as another.

    `## Request` and `## Issue` are the same section under two names: cards written before
    2026-09-18 say Request, new and edited ones say Issue (`board.ISSUE_HEADINGS`).
    """
    found, wanted = found.strip().lower(), wanted.strip().lower()
    if found == wanted:
        return True
    return found in B.ISSUE_HEADINGS and wanted in B.ISSUE_HEADINGS


#: A numbered question in `## Human QA`, and the indented `Answer:` line that settles it.
_HUMAN_QA_QUESTION_RE = re.compile(r"^(?P<number>\d+)[.)]\s+(?P<text>\S.*)$")
_HUMAN_QA_ANSWER_RE = re.compile(r"^\s+(?:[-*]\s*)?answer\s*:", re.I)


def section_text(body: str, heading: str) -> str:
    """The text under one `## ` heading, without the heading line ("" when there is none)."""
    span = _section_span(body or "", heading)
    return "" if span is None else (body or "")[span[1]:span[2]]


def unanswered_human_qa(body: str) -> list[str]:
    """The numbered `## Human QA` questions with no answer under them (#WC3E).

    The answered form is one line, and it is the *only* one: an indented line under the question
    beginning `Answer:` (docs/BOARD-FORMAT.md 2.7).  Indented, because that is what ties the
    answer to its question in a list a person edits by hand; one spelling, because a rule that
    closes cards has to be checkable by reading the file.  Everything else in the section -- the
    brief, the setup, the observations -- is prose, and prose gates nothing.
    """
    # One parser (#1AA6): `board.verified` reads the same questions and answers.
    return [question for question, answered in B.human_qa_questions(body or "") if not answered]


def _section_span(body: str, heading: str) -> tuple[int, int, int] | None:
    """(start of the heading line, start of the section text, end of the section)."""
    wanted = heading.strip()
    for match in _SECTION_RE.finditer(body):
        if not _heading_matches(match.group("heading"), wanted):
            continue
        text_start = match.end()
        if body[text_start:text_start + 1] == "\n":
            text_start += 1
        following = _SECTION_RE.search(body, match.end())
        top = _H1_RE.search(body, match.end())
        end = len(body)
        for candidate in (following, top):
            if candidate is not None:
                end = min(end, candidate.start())
        return match.start(), text_start, end
    return None


def is_owner_section(heading: str) -> bool:
    """A section the user wrote (so a rewrite has to be logged).  `## Issue` always is."""
    return heading.strip().lower() not in AGENT_SECTIONS


# ------------------------------------------------------------------- rate limiting

class RateState:
    """Creates per hour, per workspace, shared by every pane of that workspace.

    Kept in one small JSON file under `.relay/` and updated under `flock`, so two panes
    writing at the same moment cannot both slip past the ceiling.
    """

    def __init__(self, path: Path | None, clock: Callable[[], float] = time.time):
        self.path = Path(path) if path is not None else None
        self.clock = clock
        self._memory: list[float] = []

    def _load(self) -> list[float]:
        if self.path is None:
            return list(self._memory)
        try:
            data = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return []
        stamps = data.get("creates") if isinstance(data, dict) else None
        return [float(s) for s in stamps] if isinstance(stamps, list) else []

    def _store(self, stamps: list[float]) -> None:
        if self.path is None:
            self._memory = list(stamps)
            return
        self.path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_name(self.path.name + f".{os.getpid()}.tmp")
        tmp.write_text(json.dumps({"creates": stamps}), encoding="utf-8")
        os.replace(tmp, self.path)

    def recent(self) -> int:
        cutoff = self.clock() - 3600
        return sum(1 for s in self._load() if s >= cutoff)

    def claim(self, ceiling: int) -> bool:
        """Record one creation, or return False when the hour's ceiling is already used."""
        lock = None
        if self.path is not None:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            lock = os.open(str(self.path) + ".lock", os.O_WRONLY | os.O_CREAT, 0o600)
            fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            now = self.clock()
            stamps = [s for s in self._load() if s >= now - 3600]
            if len(stamps) >= ceiling:
                return False
            stamps.append(now)
            self._store(stamps)
            return True
        finally:
            if lock is not None:
                fcntl.flock(lock, fcntl.LOCK_UN)
                os.close(lock)


# --------------------------------------------------------------- whole-board cleanup

#: A cleanup rewrites many cards in one turn, so it runs on its own ceilings rather than a
#: pane turn's five creates and a hundred writes.  They are still ceilings: a run that wants
#: more than this has lost the plot and should stop and report.
CLEANUP_LIMITS = {
    "max_creates_per_turn": 60,
    "max_writes_per_turn": 400,
    "max_creates_per_hour": 200,
}

#: Where a run's changelog is written.  `issues/` holds cards and threads and nothing else,
#: so the closest fit in `issues/README.md`'s conventions is a dated evidence folder — the
#: same place an implementer's evidence for a change goes (`docs/qa_evidence/<date>-<slug>/`).
CLEANUP_EVIDENCE_SLUG = "switchboard-cleanup"

#: How many changes the summary event carries before it says "truncated"; the changelog file
#: always holds every one of them.
MAX_SUMMARY_CHANGES = 200


@dataclass
class CleanupChange:
    """One write a cleanup made (or, on a dry run, one it wanted to make)."""
    action: str
    card_id: str
    summary: str
    path: str = ""
    write_id: str = ""
    cards: list[str] = field(default_factory=list)
    proposed: bool = False

    def to_dict(self) -> dict:
        out = {"action": self.action, "card_id": self.card_id, "summary": self.summary,
               "path": self.path, "write_id": self.write_id}
        if self.cards:
            out["cards"] = list(self.cards)
        if self.proposed:
            out["proposed"] = True
        return out


@dataclass
class CleanupLog:
    """The record of one `board_cleanup` run: what it changed, and the file that says so.

    A cleanup rewrites the user's own files, so it is only as good as its changelog.  Every
    write goes through `BoardTools._record_path`, which appends here; the run ends by writing
    the whole thing to a dated file under `docs/qa_evidence/` and sending it as
    `board_cleanup_summary`, so the change can be read, judged and reverted with git.
    """
    run_id: str
    started: float = 0.0
    dry_run: bool = False
    scope: str | None = None
    note: str | None = None
    limits: dict = field(default_factory=lambda: dict(CLEANUP_LIMITS))
    changes: list[CleanupChange] = field(default_factory=list)
    refusals: list[dict] = field(default_factory=list)
    outcome: str = "running"
    report: str = ""
    cards_before: int = 0
    cards_after: int = 0
    config_before: dict = field(default_factory=dict)
    config_after: dict = field(default_factory=dict)
    finished: float = 0.0
    model: str | None = None
    changelog: str = ""

    def record(self, change: CleanupChange) -> None:
        self.changes.append(change)

    def counts(self) -> dict:
        out = {"writes": sum(1 for c in self.changes if not c.proposed),
               "proposed": sum(1 for c in self.changes if c.proposed),
               "cards_touched": len({c.card_id for c in self.changes if c.card_id})}
        for change in self.changes:
            out[change.action] = out.get(change.action, 0) + 1
        return out

    def seconds(self) -> float:
        return round(max(0.0, (self.finished or self.started) - self.started), 1)

    def summary_event(self) -> dict:
        shown = self.changes[:MAX_SUMMARY_CHANGES]
        return {"run_id": self.run_id, "outcome": self.outcome, "dry_run": self.dry_run,
                "scope": self.scope, "seconds": self.seconds(), "counts": self.counts(),
                "changes": [c.to_dict() for c in shown],
                "truncated": len(self.changes) > len(shown),
                "refusals": self.refusals[:50],
                "cards_before": self.cards_before, "cards_after": self.cards_after,
                "sections": self.sections_delta(), "changelog": self.changelog,
                "report": self.report[:4000]}

    def sections_delta(self) -> dict | None:
        if not self.config_after or self.config_before == self.config_after:
            return None
        return {"columns_before": list(self.config_before.get("columns") or []),
                "columns_after": list(self.config_after.get("columns") or []),
                "tabs_before": [str(t.get("id")) for t in (self.config_before.get("tabs") or [])],
                "tabs_after": [str(t.get("id")) for t in (self.config_after.get("tabs") or [])]}

    # ---- the changelog file ----------------------------------------------------
    def stamp(self) -> str:
        return datetime.fromtimestamp(self.started or time.time(), timezone.utc).strftime("%Y%m%dT%H%M%SZ")

    def relative_path(self) -> str:
        day = datetime.fromtimestamp(self.started or time.time(), timezone.utc).strftime("%Y-%m-%d")
        name = f"cleanup-{self.stamp()}" + ("-dry-run" if self.dry_run else "") + ".md"
        return f"docs/qa_evidence/{day}-{CLEANUP_EVIDENCE_SLUG}/{name}"

    def markdown(self) -> str:
        counts = self.counts()
        head = [f"# Board cleanup {self.run_id}",
                "",
                f"- **Run**: {self.run_id} ({'dry run, nothing written' if self.dry_run else 'applied'})",
                f"- **Finished**: {datetime.fromtimestamp(self.finished or time.time(), timezone.utc).isoformat(timespec='seconds')}"
                f" after {self.seconds()} s, outcome `{self.outcome}`",
                f"- **Model**: {self.model or 'unknown'}",
                f"- **Cards**: {self.cards_before} before, {self.cards_after} after",
                f"- **Writes**: {counts.get('writes', 0)}"
                + (f", proposed {counts['proposed']}" if counts.get("proposed") else ""),
                f"- **Scope**: {self.scope or 'the whole board'}"]
        if self.note:
            head.append(f"- **Note from the user**: {self.note}")
        sections = self.sections_delta()
        if sections:
            head += ["", "## Sections",
                     f"- columns: `{', '.join(sections['columns_before'])}` → `{', '.join(sections['columns_after'])}`",
                     f"- tabs: `{', '.join(sections['tabs_before'])}` → `{', '.join(sections['tabs_after'])}`"]
        head += ["", "## Changes", ""]
        if self.changes:
            head += ["| # | Action | Card | Summary | File |", "|---|---|---|---|---|"]
            for index, change in enumerate(self.changes, 1):
                cards = (" ← " + " ".join("#" + c for c in change.cards)) if change.cards else ""
                mark = "*(proposed)* " if change.proposed else ""
                head.append(f"| {index} | {change.action} | #{change.card_id or '—'}{cards} | "
                            f"{mark}{_cell(change.summary)} | `{change.path}` |")
        else:
            head.append("Nothing was changed.")
        if self.refusals:
            head += ["", "## Refused", ""]
            head += [f"- `{r.get('tool')}`: {_cell(str(r.get('error', '')))}" for r in self.refusals[:50]]
        head += ["", "## What the agent said", "", self.report.strip() or "(no closing message)", ""]
        return "\n".join(head)

    def write(self, repo: Path) -> str:
        """Write the changelog under the repository and return its relative path."""
        rel = self.relative_path()
        path = Path(repo) / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        B.atomic_write(path, self.markdown())
        self.changelog = rel
        return rel


def _cell(text: str) -> str:
    return " ".join(str(text).split()).replace("|", "\\|")[:300]


def _without_own_heading(text: str, heading: str) -> str:
    """A section's text with a leading copy of its own heading dropped.

    Models writing `replace_section {heading: "Plan", text: "## Plan\\n…"}` repeat the heading
    they were given, and the card then said `## Plan` twice (live Plan turn, 2026-09-18).
    """
    lines = text.lstrip("\n").split("\n")
    first = lines[0].strip() if lines else ""
    if first.startswith("#") and first.lstrip("#").strip().lower() == heading.strip().lower():
        return "\n".join(lines[1:]).lstrip("\n")
    return text


def cleanup_brief() -> str:
    """The whole-board cleanup brief, versioned beside `board_policy.md` so evals can pin it."""
    path = Path(__file__).resolve().parent / "board_cleanup_brief.md"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:                                        # pragma: no cover - packaging slip
        return ""
    return re.sub(r"<!--.*?-->", "", text, flags=re.S).strip()


# ------------------------------------------------------------ card turns (protocol 19.10)
#
# A card's "Ask the agent" became Discuss / Plan / Run (#XS6Q, owner 2026-09-18). Discuss
# and Plan are one `board_ask` turn each, told apart by `mode`; Run hands the card to a
# terminal pane and is not a turn here at all. What a mode may touch is enforced below, not
# left to the brief: a card turn runs on the Board worker, whose executor would otherwise
# offer the whole pane tool set (commands, file writes, subagents).

# Refine (#6W9X) is the third: it checks the *request* before anyone plans it — the whole board,
# closed cards too — and writes only related links, labels, a missing `## Done means` and a note.
CARD_MODES = ("discuss", "plan", "refine")

#: The worker's own tools a card turn keeps: reading, never writing or running anything.
CARD_READ_TOOLS = ("read_file", "list_directory", "load_skill", "read_skill_file")

#: The board tools each mode offers. Plan writes only its own card's `## Plan` (and may
#: comment on that card); Discuss keeps the whole ordinary set, cleanup-only tools aside.
CARD_MODE_BOARD_TOOLS = {
    "discuss": ("board_list", "board_read", "board_create_card", "board_update_card",
                "board_move_card", "board_comment"),
    "plan": ("board_list", "board_read", "board_update_card", "board_comment"),
    "refine": ("board_list", "board_read", "board_update_card", "board_comment"),
}

#: What each mode is called in a sentence (the refusals, the prompt's head).
CARD_MODE_TITLES = {"discuss": "Discuss", "plan": "Plan", "refine": "Refine"}

#: An agent console's board tools (protocol 33; 19.18 before it): the ordinary set plus merge
#: and split — merging duplicates is that conversation's headline job — plus the import.
#: `board_sections` stays with a cleanup: restructuring the whole board is a run with a preview
#: of its own, and `board_claim` with a terminal pane, which is the thing a console has not got.
CHAT_BOARD_TOOLS = ("board_list", "board_read", "board_create_card", "board_update_card",
                    "board_move_card", "board_comment", "board_merge_cards",
                    "board_split_card", "board_import_items",
                    # The page is where "which cards have no tests" is asked (protocol 31).
                    "tests_check", "tests_run")

#: The executor tools the console keeps beyond the read-only ones (#GMCF, owner 2026-09-20).
#: Since #AGNT a console keeps *every* executor tool, so this names nothing the scope has to
#: allow separately; it is kept because `set_keybinding` is still the one app-side tool that
#: writes Relay's own `keybindings.json` rather than the workspace, and `Agent.tools` offers it
#: only while a keybinding catalogue has been sent.
CHAT_APP_TOOLS = ("set_keybinding",)

#: Where a Plan turn writes. BOARD-DESIGN 12.4: plan mode writes the plan onto the card.
PLAN_HEADING = "Plan"

#: The expectations, written before the work (#WC3E, 2026-09-21): what the card is for, and how
#: failure would be recognised.  A Plan turn writes it beside `## Plan` -- those two sections and
#: nothing else -- and a verifying session checks its lines one by one, so it has to exist before
#: the implementation does.  Executing a card without one warns; it never refuses.
DONE_MEANS_HEADING = "Done means"

#: The section a person's judgement lives in (#7BM4): numbered questions for a human reviewer.
#: A question with no answer under it is unsettled, and an agent may not close the card over it
#: (`_human_qa_gate`).  An answer is an indented line beginning `Answer:` under the question;
#: docs/BOARD-FORMAT.md 2.7 is the normative form.
HUMAN_QA_HEADING = "Human QA"

MAX_SEARCH_MATCHES = 80
MAX_SEARCH_FILES = 20000
MAX_SEARCH_FILE_BYTES = 1 << 20
_SEARCH_SKIP_DIRS = frozenset({".git", ".hg", ".svn", "node_modules", "__pycache__", ".venv",
                               "venv", ".mypy_cache", ".pytest_cache", ".relay", ".ssh", ".gnupg",
                               "dist", ".cache"})

SEARCH_SPEC = spec(
    "search_files",
    "Search the workspace's text files for a regular expression (Python syntax, case-insensitive "
    "unless it has an uppercase letter). Returns up to 80 matching lines as path:line: text. "
    "Read-only. Use it to find where something is defined before reading the file. When the workspace "
    "is the home directory or /, pass `path`: crawling all of it is refused as too slow.",
    {"pattern": {"type": "string", "description": "Regular expression, e.g. 'def board_ask|board_ask\\('."},
     "path": {"type": "string", "description": "Workspace-relative directory or file to search; default '.'."},
     "glob": {"type": "string", "description": "Only files whose name matches, e.g. '*.py' or '*.cpp'."}},
    ["pattern"])


def card_block(board: B.Board, card: B.Card) -> str:
    """The card as one text block: front matter, body (16 KiB cap), tasks and the last 10 entries.

    The *same* block `ask {cards: [...]}` sends for a `#K7Q2` reference (protocol 19.6), so a card
    that arrives with the prompt and a card `board_claim` hands back read identically. It lives in
    `board_protocol.seed_block`, which imports this module, so the import is made here rather than
    at the top — there is one builder, not two.
    """
    from . import board_protocol                       # late: it imports this module
    return board_protocol.seed_block(board, card)


def card_brief(mode: str) -> str:
    """The Discuss or Plan brief (`board_discuss_brief.md`, `board_plan_brief.md`), beside the policy."""
    path = Path(__file__).resolve().parent / f"board_{mode}_brief.md"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:                                        # pragma: no cover - packaging slip
        return ""
    return re.sub(r"<!--.*?-->", "", text, flags=re.S).strip()


@dataclass
class ConsoleScope:
    """An agent console's board tools (protocol 33, card #AGNT; 19.18 before it).

    It rides in the `card_scope` slot — that is where the board looks for a turn's scope — so it
    answers the same three questions `CardScope` does, and `chat` marks it apart for the
    cleanup-only fence in `run`: merging duplicates is this conversation's headline job.

    **It fences nothing off.** Until 2026-09-20 this scope withheld the shell and the file
    writes (§19.18: "No shell, no file writes: code is a card's Run"), which predates the
    owner's rule that a context specialises an agent without fencing it — *"agents are
    specialized for the given pane context, but the general rule/approach is that agents have
    access to all systems and can work across panes and contexts"* — and which a board-less
    helper got round by accident anyway, because the scope lived on the board. So `allows` is
    now true of everything: what is offered is the whole executor plus the board set below, and
    the gates that remain are the owner's own (the `settable` / `agent_safe` markers and the
    Options › Agent toggle, #FEJQ decisions 1–3). A card's **Plan** turn is the one turn that
    still writes nothing but its own `## Plan`, and that is `CardScope`, a rule about the stage.

    Since #GMCF (owner, 2026-09-20) it also keeps `set_keybinding`: the console in the Actions
    pane is the palette "with its keyboard shortcut beside it", and the one thing asked of it
    there is to move a shortcut. `Agent.tools` offers it only while the GUI has sent a
    keybinding catalogue.
    """
    chat: bool = True
    mode: str = "console"

    def allows(self, name: str) -> bool:
        return True

    def tool_specs(self, executor_specs: list[dict]) -> list[dict]:
        """The turn's tool list: everything the executor offers, search_files, the board tools."""
        return list(executor_specs) + console_board_specs()

    def refusal(self, name: str) -> str:            # pragma: no cover - `allows` refuses nothing
        return f"{name} is not available in this console."


#: The name this scope had while it was the Board page agent's alone (19.18).  Kept so a
#: caller written against that name still compiles; there is one class.
ChatScope = ConsoleScope


def console_board_specs() -> list[dict]:
    """The board half of a console's tool list: `search_files` and `CHAT_BOARD_TOOLS`.

    Separate from `ConsoleScope.tool_specs` because since #AGNT a console takes the *pane's*
    branch of `Agent.tools` — the whole executor, in the order a pane has them — and the board
    appends this, which is what makes "one tool set everywhere" true rather than asserted.
    """
    specs = [s for s in list(TOOL_SPECS) + list(CLEANUP_TOOL_SPECS)
             if s["function"]["name"] in CHAT_BOARD_TOOLS]
    return [dict(SEARCH_SPEC)] + [dict(s) for s in specs]


@dataclass
class CardScope:
    """One Discuss or Plan turn on one card: the tools it may call and the card it is about.

    It had a `tool_specs` of its own until card #CTRN — the mode's board tools and the read-only
    file tools, which is what the turn was *offered*.  A card turn is an ordinary console turn
    now: it is offered the console's list, every turn, and what the stage forbids is refused when
    it is called (`Agent.set_card_turn`, `_check_card_scope`, and `refusal` below, which names
    Run).  `allows` and `refusal` did not move an inch; only the list did.
    """
    mode: str
    card_id: str

    def allows(self, name: str) -> bool:
        return (name in CARD_READ_TOOLS or name == "search_files"
                or name in CARD_MODE_BOARD_TOOLS.get(self.mode, ()))

    def refusal(self, name: str) -> str:
        what = CARD_MODE_TITLES.get(self.mode, "Discuss")
        return (f"{name} is not available in a {what} turn on #{self.card_id}: it reads the "
                "repository (read_file, list_directory, search_files) and writes only through the "
                "board tools. Writing code is Run's job — the owner hands the card to a "
                "terminal pane for that.")


def search_workspace(root: Path, args: dict) -> dict:
    """`search_files`: a read-only grep over the workspace, with the file tools' secret guard."""
    from .tools import Workspace, wide_root            # late: tools imports nothing of ours
    if set(args) - {"pattern", "path", "glob"}:
        raise BoardToolError("search_files takes pattern, path and glob.")
    pattern = args.get("pattern")
    if not isinstance(pattern, str) or not pattern or len(pattern) > 500:
        raise BoardToolError("pattern must be a regular expression of 1-500 characters.")
    try:
        regex = re.compile(pattern, 0 if any(c.isupper() for c in pattern) else re.I)
    except re.error as exc:
        raise BoardToolError(f"pattern is not a valid regular expression: {exc}") from exc
    glob = args.get("glob")
    if glob is not None and (not isinstance(glob, str) or len(glob) > 100):
        raise BoardToolError("glob must be a short file-name pattern such as '*.py'.")
    workspace = Workspace(root)
    rel = args.get("path") or "."
    try:
        start = workspace.root if rel in (".", "./") else workspace.resolve(str(rel))
    except (ValueError, OSError) as exc:
        raise BoardToolError(str(exc)) from exc
    # Card #2Y96, the same cost guard run_command has: a card turn whose workspace is the home
    # directory or `/` must be given a path before it crawls. The ceilings below still apply.
    if why := wide_root(start):
        raise BoardToolError(f"Searching all of {start} ({why}) would take minutes and return little, "
                             f"so Relay refuses it — this is a cost limit, not a permission one. Pass "
                             f"`path` naming the directory to search, e.g. {start / '<subdirectory>'}.")
    import fnmatch

    def secret(parts) -> bool:
        return any(p == ".env" or p.startswith(".env.") or p in {"id_rsa", "id_ed25519"}
                   or p.endswith((".pem", ".key")) for p in parts)

    matches: list[str] = []
    scanned = 0
    truncated = False
    walker = [(start.parent, [], [start.name])] if start.is_file() else os.walk(start)
    for folder, dirs, names in walker:
        folder = Path(folder)
        dirs[:] = sorted(d for d in dirs if d not in _SEARCH_SKIP_DIRS
                         and not (folder / d).is_symlink())
        for name in sorted(names):
            if glob and not fnmatch.fnmatch(name, glob):
                continue
            path = folder / name
            relative = path.relative_to(workspace.root)
            if path.is_symlink() or secret(relative.parts):
                continue
            scanned += 1
            if scanned > MAX_SEARCH_FILES:
                truncated = True
                break
            try:
                if path.stat().st_size > MAX_SEARCH_FILE_BYTES:
                    continue
                data = path.read_bytes()
            except OSError:
                continue
            if b"\0" in data[:4096]:
                continue                                   # binary
            for number, line in enumerate(data.decode("utf-8", "replace").splitlines(), 1):
                if regex.search(line):
                    matches.append(f"{relative}:{number}: {line.strip()[:200]}")
                    if len(matches) >= MAX_SEARCH_MATCHES:
                        truncated = True
                        break
            if truncated:
                break
        if truncated:
            break
    return {"matches": matches, "count": len(matches), "truncated": truncated,
            "files_scanned": min(scanned, MAX_SEARCH_FILES)}


# ------------------------------------------------------------------------- writes

@dataclass
class WriteRecord:
    """One reversible agent write (design 6.3, "Undo")."""
    write_id: str
    action: str
    card_id: str
    path: Path
    summary: str
    at: float
    before: bytes | None = None        # None = the file did not exist (a creation)
    thread_path: Path | None = None
    thread_size: int = 0
    moved_from: Path | None = None
    undone: bool = False
    #: Files beyond the primary card this one write also touched — a merge rewrites and
    #: closes every card it folded in, a split creates one file per piece.  Each entry is
    #: (path now, bytes before or None for a file this write created, path it was moved from).
    others: list[tuple[Path, bytes | None, Path | None]] = field(default_factory=list)


@dataclass
class ToolContext:
    """Who is writing, for the thread entries and the activity events.

    `preset` and `model` together are what the worker knows about *itself*, and `signature()`
    turns them into the canonical `provider/model` a card records as its `implemented_by` or
    `verified_by` (card #T71W).  Both are set per turn by the agent (`Agent.ask`); a guest
    writing through the bridge sets neither, and then the agent's own `implemented_by`
    argument is what the card gets.
    """
    actor: str = "agent"
    model: str | None = None
    preset: str | None = None
    pane: str | None = None
    turn_id: str | None = None
    session_id: str | None = None
    #: The skills this turn has loaded (#MSJ0): skill id → its `profile` (`{}` for a skill with
    #: none).  `Agent` records a `load_skill` call and a `/name` invocation here and
    #: `BoardTools.begin_turn` clears it, so it is exactly the skills in the turn's context; when
    #: one of them carries verify keys, `board_claim` and `board_update_card` fill a card's
    #: missing `verify` from it (`BoardTools._verify_from_skills`).
    skills: dict[str, dict] = field(default_factory=dict)
    #: skill id → `cases.skill_version` of its SKILL.md (#95VZ), for the ledger row the turn's
    #: end writes; "" when the agent could not read the file.
    skill_versions: dict[str, str] = field(default_factory=dict)

    def signature(self) -> str:
        """This worker's own `provider/model`, or "" when it cannot know it."""
        return QA.signature(self.preset, self.model)

    def skill_loaded(self, skill_id, profile, version: str = "") -> None:
        """Record that this turn loaded `skill_id`, with its `profile` block (may be empty)
        and, when the caller knows it, the `version` of its SKILL.md (#95VZ)."""
        if isinstance(skill_id, str) and skill_id.strip():
            self.skills[skill_id.strip()] = dict(profile) if isinstance(profile, dict) else {}
            if isinstance(version, str) and version:
                self.skill_versions[skill_id.strip()] = version

    def attrs(self) -> dict:
        out = {}
        if self.model:
            out["model"] = self.model
        if self.pane:
            out["pane"] = str(self.pane)
        if self.turn_id:
            out["turn"] = f"{self.session_id}/{self.turn_id}" if self.session_id else str(self.turn_id)
        return out


def find_board_root(workspace: str | os.PathLike | None,
                    explicit_dir: str | os.PathLike | None = None,
                    folder: str | None = None) -> Path | None:
    """The board directory that governs `workspace`, or None when there is none.

    The one place the backend decides which board a message is about, so the worker, the agent's
    tools and `#K7Q2` attachments all land on the same tree, and so does the GUI, which has always
    walked up to the nearest ancestor holding a board.  Before 2026-09-18 the backend took
    `<workspace>/issues` literally, so a pane opened in a subdirectory of a project saw no board
    at all while the window's board pane showed one.

    **The rule, and the C++ `relay::boardRootFor` must match it exactly:** an explicit `board.dir`
    (protocol 19.1) always wins.  Otherwise the walk starts at the resolved workspace and climbs to
    the filesystem root; at each directory the candidates are tried in `B.BOARD_FOLDERS` order —
    `board/board.yaml`, then `.switchboard/`, `switchboard/` and `issues/board.yaml` — and the
    first hit wins.  So the **nearest ancestor** wins over a further one whatever its spelling, and
    a single directory holding more than one of them is read as the first in that order.

    `folder` is the folder a board **would** go in, for an `explicit_dir` that names a project with
    no board yet; it never affects the walk, which only ever finds folders that exist.

    **An absent or empty workspace has no board**: the process's cwd, the environment and this
    file's location are never consulted.  A worker started from the directory Relay was launched in
    must not adopt *that* project's board because the `configure` it was sent named no workspace —
    which is exactly what `workspace: ""` used to do, quietly opening the launch directory's 186
    cards in a window that pointed somewhere else.
    """
    if explicit_dir is not None and str(explicit_dir).strip():
        root = named_board_root(explicit_dir, folder)
        return root if (root / B.BOARD_CONFIG).is_file() else None
    if workspace is None or not str(workspace).strip():
        return None
    try:
        here = Path(workspace).expanduser().resolve()
    except OSError:                                     # pragma: no cover - unreadable path
        return None
    for directory in (here, *here.parents):
        root = B.board_folder(directory)
        if root is not None:
            return root
    return None


def named_board_root(explicit_dir: str | os.PathLike, folder: str | None = None) -> Path:
    """The board directory a `board.dir` (or a `project`) names, whether or not it exists yet.

    It may name the board folder itself or the project that holds one, because the GUI has both in
    hand and should not have to guess which spelling the worker wants.  An existing `board.yaml`
    decides it — the folder's own, then `B.BOARD_FOLDERS` in order.  With none of them present the
    name decides: a directory already called `board`, `.switchboard`, `switchboard` or `issues` is
    taken as the board folder, and anything else is a project, whose board **would** go in
    `folder` — the `board.folder` of the `configure` that pointed this worker — defaulting to
    `board`.  Nothing is created here either way.

    Resolved, like the walk's answer in `find_board_root`, because `root` is what a GUI routes
    events by: two spellings of one directory must not look like two boards.
    """
    here = Path(explicit_dir).expanduser().resolve()
    if (here / B.BOARD_CONFIG).is_file():
        return here
    found = B.board_folder(here)
    if found is not None:
        return found
    if here.name in B.BOARD_FOLDERS:
        return here
    return here / (folder if folder in B.BOARD_FOLDERS else B.DEFAULT_BOARD_FOLDER)


def board_at(root: str | os.PathLike) -> B.Board:
    """A `Board` on `root`, with `repo` the project directory that holds it.

    The repo is where `.relay/board-rate.json`, the cleanup changelogs and every path in a
    `board_activity` are relative to, so it is the project root rather than whichever subdirectory
    the pane happens to be open in.
    """
    root = Path(root)
    return B.Board(root, root.parent)


def board_for(workspace: str | os.PathLike | None,
              explicit_dir: str | os.PathLike | None = None,
              folder: str | None = None) -> B.Board | None:
    """`find_board_root`, as a `Board`.  None when no board exists for this workspace."""
    root = find_board_root(workspace, explicit_dir, folder)
    return None if root is None else board_at(root)


class BoardInit:
    """"Initialize a project and create a Board here?" — the one round trip (protocol 19.12).

    One instance per worker, shared by the owner's tools and the agent's, so a yes or a no is the
    pane's and not one instance's.  The shape is `terminal_handoff.TerminalHandoff`'s, for the same
    reason: the request goes out as an event, the GUI answers on the protocol thread, and whoever
    is waiting is woken.  Two callers use it differently and both are here so the difference is
    visible:

    * the **agent's** tool call runs on the turn thread, so `ask_and_wait` blocks it until the
      answer arrives.  There is no timeout — the pane owns the dialog and always answers it — and
      Stop raises `Cancelled` out of the tool, which is how every other blocking tool ends;
    * the **owner's** `board_create` arrives on the protocol thread, which is the very thread that
      would have to read the answer, so it cannot block.  `ask` parks a callback instead and
      `board_protocol` replays the write when the answer comes.
    """

    def __init__(self, emit: Callable[[dict], None], cancel: threading.Event | None = None):
        self.emit = emit
        #: The agent's `cancel_event`, set by the worker once there is an agent.  Stop while the
        #: dialog is open must end the turn, not leave a thread parked on it.
        self.cancel = cancel
        #: The user said no: nothing asks again until the board is re-pointed or initialized.
        self.declined = False
        self._lock = threading.Lock()
        self._pending: dict[str, list] = {}
        self._next = 0

    def ask(self, *, project: str, directory: str | os.PathLike, reason: str,
            title: str = "", request_id=None, callback: Callable[[bool], None] | None = None) -> str:
        """Emit `board_init_request` and return its id.  `callback(accepted)` runs when answered."""
        if reason not in INIT_REASONS:                   # pragma: no cover - callers pass a constant
            reason = "agent-card"
        with self._lock:
            self._next += 1
            init_id = f"bi-{self._next}"
            self._pending[init_id] = [threading.Event(), None, callback]
        event = {"event": "board_init_request", "id": init_id, "root": str(directory),
                 "project": str(project), "dir": str(directory), "reason": reason}
        if title:
            event["title"] = str(title)[:200]
        if request_id is not None:
            event["request_id"] = request_id
        self.emit(event)
        return init_id

    def ask_and_wait(self, *, project: str, directory: str | os.PathLike, reason: str,
                     title: str = "") -> bool:
        """`ask`, then block this (turn) thread until the user answers or stops the turn."""
        init_id = self.ask(project=project, directory=directory, reason=reason, title=title)
        done = self._pending[init_id][0]
        while not done.wait(0.05):
            if self.cancel is not None and self.cancel.is_set():
                self._take(init_id)
                raise Cancelled("Stopped.")
        return bool(self._take(init_id))

    def _take(self, init_id: str) -> bool:
        with self._lock:
            entry = self._pending.pop(init_id, None)
        return bool(entry and entry[1])

    def answer(self, reply: dict) -> dict:
        """`board_init_answer {id, accept}` from the GUI, on the protocol thread."""
        if not isinstance(reply, dict):                  # pragma: no cover - dispatch checks first
            raise ValueError("board_init_answer must be an object.")
        init_id = reply.get("id")
        accept = reply.get("accept")
        if type(accept) is not bool:
            raise ValueError("board_init_answer needs accept: true or false.")
        with self._lock:
            entry = self._pending.get(init_id) if isinstance(init_id, str) else None
            if entry is None:
                # The turn was stopped, or the answer is late: nothing is waiting for it. The no
                # is still remembered, so a second card does not reopen the dialog.
                if not accept:
                    self.declined = True
                return {"id": init_id, "accept": accept, "pending": False}
            entry[1] = accept
            callback = entry[2]
            if callback is not None:
                self._pending.pop(init_id, None)
            entry[0].set()
        if not accept:
            self.declined = True
        if callback is not None:
            callback(accept)                              # the owner's parked write, replayed
        return {"id": init_id, "accept": accept, "pending": True}

    def fail_pending(self) -> None:
        """Answer everything still waiting with a no (the turn ended, or the pane went away)."""
        with self._lock:
            entries = list(self._pending.values())
            self._pending.clear()
        for entry in entries:
            entry[1] = False
            entry[0].set()


class BoardTools:
    """The `board_*` tools for one pane (or for the Board worker)."""

    def __init__(self, board: B.Board, *, emit: Callable[[dict], None] | None = None,
                 autonomy: str | None = None, limits: dict | None = None,
                 context: ToolContext | None = None, state_path: Path | str | None = None,
                 clock: Callable[[], float] = time.time, enforce_limits: bool = True,
                 duplicate_check: bool = True, state: str = "ready", project: str | None = None,
                 init=None, pane_token: str | None = None, qa: dict | None = None,
                 workspace: str | os.PathLike | None = None):
        self.board = board
        #: The workspace of the pane these tools serve, or None for the Board worker and for
        #: tests.  The case ledger's confidential rows (#95VZ) are returned only when it is
        #: this board's own project — `own_workspace()` — so a pane pointed at another
        #: project's board reads that board's ledger minus the rows that name nothing.
        self.workspace = Path(workspace).expanduser() if workspace else None
        #: This pane's session token, from `configure {pane_token}` (protocol 19.19), or None for
        #: the Board worker and for tests.  `board_claim` writes it onto the card as
        #: `session` and onto its progress entry as `pane_token`, so the Board can draw the
        #: claim as a link that reveals the pane doing the work.
        self.pane_token = check_pane_token(pane_token)
        #: The cards this pane has claimed this conversation (#R9G7), newest last.  Named in the
        #: system prompt so the model never has to remember or retype a token.
        self.claimed: list[str] = []
        #: "ready" or "uninitialized" (protocol 19.12).  An uninitialized board is not on disk:
        #: these tools offer `board_create_card` alone, and creating a card asks the user first.
        self.state = state if state in BOARD_STATES else "ready"
        #: The project this board belongs to, carried through from `configure`/`set_board` onto
        #: the events.  It is a label for the GUI: no file is ever looked for under it.
        self.project = str(project) if project else str(board.repo)
        #: The shared `BoardInit` round trip, or None when nothing may be initialized here.
        self.init = init
        #: Called after this board is created, so the worker can re-point and the agent's system
        #: prompt can go from the one-line note to the full policy block.
        self.on_created: Callable[[], None] | None = None
        self.emit = emit or (lambda event: None)
        self.clock = clock
        self.context = context or ToolContext()
        config = board.config()
        agent_config = config.get("agent") if isinstance(config.get("agent"), dict) else {}
        #: The QA policy floor (#C3Q2): `board.yaml qa:` over `configure.qa` (Options › Agent ›
        #: QA's one switch) over `qa_policy.DEFAULTS`.  Applied silently to every `verify`
        #: block these tools write (`_apply_qa_floor`), decides whether a verifier's pass may
        #: close a card (`_qa_ask_gate`), and is stated in one line on every `board_read`.
        self.qa_policy = QP.parse(config.get("qa"), qa)
        # `autonomy: off` in board.yaml reads back as the YAML boolean false, so map it here
        # rather than making every board.yaml quote the word.
        raw = agent_config.get("autonomy")
        if raw is False:
            raw = "off"
        elif raw is True:
            raw = "auto"
        self.autonomy = autonomy or str(raw or "suggest")
        if self.autonomy not in AUTONOMY:
            self.autonomy = "suggest"
        self.limits = dict(DEFAULT_LIMITS)
        for key in DEFAULT_LIMITS:
            if isinstance(agent_config.get(key), int):
                self.limits[key] = max(0, int(agent_config[key]))
        for key, value in (limits or {}).items():
            if key in self.limits and isinstance(value, int):
                self.limits[key] = max(0, int(value))
        if state_path is None:
            state_path = Path(board.repo) / ".relay" / "board-rate.json"
        self.rate = RateState(state_path, clock)
        # The guardrails exist to keep an *agent* honest; the owner's own writes from the
        # Board pane go through the same code with them turned off.
        self.enforce_limits = enforce_limits
        self.duplicate_check = duplicate_check
        self.writes: dict[str, WriteRecord] = {}
        self._order: list[str] = []
        self.creates_this_turn = 0
        self.writes_this_turn = 0
        #: The cards this turn wrote to, in order (#95VZ): the row the turn's end writes for a
        #: loaded profiled skill names the last of them.
        self.cards_this_turn: list[str] = []
        #: Set while a `board_cleanup` turn runs (protocol 19.9): it raises the per-turn
        #: ceilings, offers the merge/split/sections tools, and records every write.
        self.cleanup: CleanupLog | None = None
        #: Set while a card's Discuss or Plan turn runs (protocol 19.10, #XS6Q): what that mode
        #: may call, and the card a Plan turn may write to. None for a pane's own turns. An
        #: agent console sets it to a `ConsoleScope` when it is configured, which is why the
        #: type is loose; a **card** console has both — the `ConsoleScope` between turns and a
        #: `CardScope` for the length of each one (#CTRN) — and `end_card_turn` puts the
        #: console's back.
        self.card_scope: CardScope | ConsoleScope | None = None
        #: Whether these tools belong to an agent **console** (#AGNT). Asked directly since card
        #: #CTRN rather than inferred from whatever is in the `card_scope` slot: a card console
        #: opens a `CardScope` for each turn, and reading "is this a console" off that slot made
        #: the board's half of the tool list change under the turn — the one place a per-turn
        #: scope could still re-prefill a cached request, which is the whole thing #CTRN's
        #: decision 3 is about.
        self.console = False
        #: Set while a turn that writes nothing runs (the survey of a fresh board, 19.18): every
        #: write tool refuses, so what the agent offers stays an offer until the owner says yes.
        self.readonly = False
        #: This project's `tests_protocol.TestsCommands` (protocol 31, #7BM4), behind
        #: `tests_check` and `tests_run`.  Made on first use; see `_tests`.
        self._tests_commands = None

    # ---- events ---------------------------------------------------------------
    def _emit_board(self, event: dict) -> None:
        """Send a board event naming the board it happened on (protocol 19.2).

        One window can have several projects open, so every `board_*` event carries `root` — the
        `issues/` directory it is about — and a GUI routes by it instead of assuming the events it
        receives belong to whatever board it asked about last.
        """
        self.emit({"root": str(self.board.root), **event})

    # ---- initialization, with the user's consent (protocol 19.12) --------------
    def exists(self) -> bool:
        """Whether this board is on disk yet.  An `uninitialized` one is not."""
        return self.board.config_path.is_file()

    def create_board(self) -> list[str]:
        """Scaffold this board and announce it.  The caller has the user's yes.

        Nothing else writes `board.yaml`: the owner's rule of 2026-09-18 is that a project gets a
        Board only when the user answers "Initialize a project and create a Board
        here?", so every path to this method runs through `board_init` (the GUI asked) or a
        `board_init_request` the user accepted.
        """
        files = B.scaffold(self.board)
        self.state = "ready"
        self._emit_board({"event": "board_created", "workspace": str(self.board.repo),
                          "project": self.project, "files": files})
        if self.on_created is not None:
            self.on_created()
        return files

    def ensure_board(self, *, title: str = "", reason: str = "agent-card") -> bool:
        """Before a write: make sure there is a board, asking the user once.  True if it created one.

        The agent's instance runs this on the turn thread, so it blocks on the round trip exactly
        as `terminal_handoff` does — the user's answer arrives on the protocol thread, a Stop
        raises `Cancelled`, and no timeout is needed because the pane always answers a dialog it
        opened.  The owner's instance never reaches here in the uninitialized state: a
        `board_create` message is parked by `board_protocol` instead, because the protocol thread
        is the one that would have to read the answer.
        """
        if self.state != "uninitialized" or self.exists():
            return False
        if self.init is None:                            # pragma: no cover - always wired in the worker
            raise BoardToolError(NO_BOARD_TEXT, code="board_not_initialized")
        if self.init.declined:
            raise BoardToolError(NO_BOARD_TEXT, code="board_not_initialized")
        accepted = self.init.ask_and_wait(project=self.project, directory=self.board.root,
                                          reason=reason, title=title)
        if not accepted:
            raise BoardToolError(NO_BOARD_TEXT, code="board_not_initialized")
        self.create_board()
        return True

    # ---- lifecycle ------------------------------------------------------------
    @classmethod
    def for_workspace(cls, workspace: str | os.PathLike, **kwargs) -> "BoardTools | None":
        """The tools for a workspace, or None when it has no Board or autonomy is off."""
        board = board_for(workspace)
        if board is None:
            return None
        tools = cls(board, **kwargs)
        return None if tools.autonomy == "off" else tools

    def begin_turn(self, turn_id: str | None = None) -> None:
        self.creates_this_turn = 0
        self.writes_this_turn = 0
        self.context.turn_id = turn_id
        self.context.skills.clear()
        self.context.skill_versions.clear()
        self.cards_this_turn = []

    def begin_cleanup(self, run_id: str, *, dry_run: bool = False, scope: str | None = None,
                      note: str | None = None, limits: dict | None = None) -> CleanupLog:
        """Enter cleanup mode: raised ceilings, the merge/split/sections tools, a changelog."""
        ceilings = dict(CLEANUP_LIMITS)
        for key, value in (limits or {}).items():
            if key in ceilings and isinstance(value, int):
                ceilings[key] = max(0, int(value))
        log = CleanupLog(run_id=run_id, started=self.clock(), dry_run=bool(dry_run),
                         scope=scope, note=note, limits=ceilings,
                         model=self.context.model,
                         cards_before=len(self.board.card_paths()),
                         config_before=self.board.config())
        log.changelog = log.relative_path()
        self.cleanup = log
        return log

    def end_cleanup(self, outcome: str, report: str = "") -> CleanupLog | None:
        """Leave cleanup mode and finish the changelog.  Returns the log, or None."""
        log, self.cleanup = self.cleanup, None
        if log is None:
            return None
        log.outcome = outcome
        log.report = report or ""
        log.finished = self.clock()
        log.cards_after = len(self.board.card_paths())
        log.config_after = self.board.config()
        log.model = log.model or self.context.model
        return log

    def limit(self, key: str) -> int:
        """The ceiling in force: a cleanup's raised one, or the pane turn's."""
        if self.cleanup is not None:
            return int(self.cleanup.limits.get(key, self.limits[key]))
        return int(self.limits[key])

    def tool_specs(self) -> list[dict]:
        if self.state == "uninitialized":
            # Nothing to read, move or comment on until a board exists; the one tool that can
            # bring one into being is offered, and calling it asks the user (protocol 19.12).
            return [dict(s) for s in TOOL_SPECS
                    if s["function"]["name"] in UNINITIALIZED_TOOLS]
        if self.console:
            # An agent console (#AGNT): the ordinary set plus merge, split and the import, and
            # `search_files`, which a console reaches through the board rather than the executor.
            # The flag rather than the scope, so a card turn's `CardScope` does not switch this
            # list out from under a conversation that is mid-prefix (#CTRN).
            return console_board_specs()
        specs = list(TOOL_SPECS) + (list(CLEANUP_TOOL_SPECS) if self.cleanup is not None else [])
        return [dict(s) for s in specs]

    def handles(self, name: str) -> bool:
        return name in ALL_TOOL_NAMES or (name == "search_files" and self.card_scope is not None)

    def begin_card_turn(self, mode: str, card_id: str) -> CardScope:
        """A Discuss, Plan or Refine turn on one card starts: narrow the tools to what the mode offers."""
        if mode not in CARD_MODES:
            raise BoardToolError(f"mode must be one of {', '.join(CARD_MODES)}.")
        self.card_scope = CardScope(mode, normalize_id(card_id))
        return self.card_scope

    def end_card_turn(self) -> None:
        """The turn is over: the console's own scope comes back, or nothing does (#CTRN).

        A card's tools are a console's, so taking the `CardScope` away has to leave the
        `ConsoleScope` that was there before it — otherwise `search_files` and the console's
        board set disappear between one turn on a card and the next.
        """
        self.card_scope = ConsoleScope() if self.console else None

    def begin_console(self) -> ConsoleScope:
        """These tools belong to an agent console (#AGNT): board tools, merge and split included.

        Unlike a card turn's scope this is set once, when the console's agent is configured, and
        never taken down: a console *is* that scope, it does not enter and leave one. The
        read-only survey turn is `Agent.set_readonly`, which sets `readonly` below for the length
        of one turn — nothing is written until the owner confirms, and that is enforced here
        rather than asked for in the brief.
        """
        self.console = True
        self.card_scope = ConsoleScope()
        return self.card_scope

    def begin_chat_turn(self, *, readonly: bool = False) -> ConsoleScope:
        """`begin_console` with a one-turn `readonly`, for callers written before #AGNT."""
        scope = self.begin_console()
        self.readonly = bool(readonly)
        return scope

    def end_chat_turn(self) -> None:
        self.console = False
        self.card_scope = None
        self.readonly = False

    def _check_card_scope(self, name: str, args: dict) -> None:
        """A Plan turn writes its own card's `## Plan` and `## Done means`; Discuss has no extra rule.

        `## Done means` joined `## Plan` with #WC3E: the Plan brief tells the turn to write the
        expectations before the plan, so the scope that enforces the stage has to allow exactly
        those two sections and still nothing else.
        """
        scope = self.card_scope
        if scope is None:
            return
        if not scope.allows(name):
            raise BoardToolError(scope.refusal(name), code="board_mode_refused", mode=scope.mode)
        if scope.mode not in ("plan", "refine") or name not in WRITE_TOOLS:
            return
        what = CARD_MODE_TITLES[scope.mode]
        target = normalize_id(args.get("id")) if args.get("id") else ""
        if scope.mode == "refine":
            if target != scope.card_id:
                raise BoardToolError(
                    f"A Refine turn writes only to #{scope.card_id}, the card being refined. Name "
                    f"#{target or '?'} in your note and add it to this card's links.related instead.",
                    code="board_mode_refused", mode="refine")
            if name == "board_update_card":
                self._check_refine_update(scope.card_id, args)
            return
        if target != scope.card_id:
            raise BoardToolError(
                f"A Plan turn writes only to #{scope.card_id}, the card being planned. Mention "
                f"#{target or '?'} in the plan instead of changing it.",
                code="board_mode_refused", mode="plan")
        if name == "board_update_card":
            extra = set(args) - {"id", "base_hash", "replace_section", "append_section"}
            blocks = [args.get(k) for k in ("replace_section", "append_section") if args.get(k) is not None]
            headings = {str(b.get("heading") or "").strip().lstrip("#").strip().lower()
                        for b in blocks if isinstance(b, dict)}
            allowed_headings = {PLAN_HEADING.lower(), DONE_MEANS_HEADING.lower()}
            if extra or not blocks or not headings or headings - allowed_headings:
                raise BoardToolError(
                    f"A Plan turn writes the card's `## {PLAN_HEADING}` and `## {DONE_MEANS_HEADING}` "
                    "sections and nothing else: call board_update_card with replace_section "
                    f"{{heading: \"{PLAN_HEADING}\", text}}. The title, the issue, labels and "
                    "status are Discuss's to change.",
                    code="board_mode_refused", mode="plan")

    def _check_refine_update(self, card_id: str, args: dict) -> None:
        """What a Refine turn may change on its own card (#6W9X), and nothing else.

        `## Done means` only when the card has none; `labels` only with words the board already
        carries; `links` only in `related` — `links` is one front-matter object, so a partial one
        would silently drop the card's commits and evidence. The issue, the plan and the title are
        the owner's and Plan's, and the refusal says which button writes them.
        """
        def refuse(text: str):
            raise BoardToolError(text + " A Refine turn writes this card's links.related, its labels "
                                 f"and a missing `## {DONE_MEANS_HEADING}`, and comments; the issue "
                                 "is Discuss's to change and the plan is Plan's.",
                                 code="board_mode_refused", mode="refine")
        extra = set(args) - {"id", "base_hash", "replace_section", "append_section", "fields"}
        if extra:
            refuse(f"{', '.join(sorted(extra))} is not a Refine turn's to change.")
        card = self.board.card_by_id(card_id)
        if card is None:
            return                                   # the write itself says the card is gone
        blocks = [args.get(k) for k in ("replace_section", "append_section") if args.get(k) is not None]
        for block in blocks:
            heading = str((block or {}).get("heading") if isinstance(block, dict) else "")
            heading = heading.strip().lstrip("#").strip()
            if heading.lower() != DONE_MEANS_HEADING.lower():
                refuse(f"`## {heading or '?'}` is not a Refine turn's section.")
            if _section_span(card.body, DONE_MEANS_HEADING):
                refuse(f"#{card_id} already has a `## {DONE_MEANS_HEADING}`; Refine leaves it alone.")
        fields = args.get("fields")
        if fields is None:
            if not blocks:
                refuse("Nothing to write.")
            return
        if not isinstance(fields, dict) or set(fields) - {"labels", "links"}:
            keys = ", ".join(sorted(set(fields) - {"labels", "links"})) if isinstance(fields, dict) else "fields"
            refuse(f"{keys} is not a Refine turn's to change.")
        if "links" in fields:
            new, old = fields["links"], dict(card.front.get("links") or {})
            if not isinstance(new, dict) or \
                    {k: v for k, v in new.items() if k != "related"} != \
                    {k: v for k, v in old.items() if k != "related"}:
                refuse("links may change only in `related`: send the card's whole links object "
                       "with ids added to related, and every other key exactly as it is.")
        if "labels" in fields:
            labels = fields["labels"]
            if not isinstance(labels, list):
                refuse("labels must be a list.")
            own = {str(l) for l in (card.front.get("labels") or [])}
            known = {str(l) for other in self.board.cards() if other.id != card_id
                     for l in (other.front.get("labels") or [])}
            new_words = sorted({str(l) for l in labels} - own - known)
            if new_words:
                refuse(f"{', '.join(new_words)} is not a label this board uses yet: suggest it in "
                       "your note and the owner decides.")

    # ---- dispatch -------------------------------------------------------------
    def preview(self, name: str, args: dict) -> str:
        """One human-readable block for the pane's tool line (no side effects)."""
        if not isinstance(args, dict):
            return name.upper().replace("_", " ")
        head = name.replace("board_", "").replace("_", " ").upper()
        bits = []
        for key in ("id", "tab", "status", "title", "kind", "query", "reason", "pattern", "glob"):
            if args.get(key):
                bits.append(f"{key}: {str(args[key])[:120]}")
        return f"BOARD {head}\n\n" + ("\n".join(bits) or "(no arguments)")

    def run(self, name: str, args: dict, *, by_owner: bool = False) -> dict:
        """Run one Board tool.  `by_owner` is the GUI acting for the person at the keyboard.

        The cleanup-only tools are fenced off because an *agent* must not restructure the board
        in the middle of an ordinary turn — not because the structure is off limits.  The owner
        editing the section list in the gear is the case the fence was never about, so
        `OWNER_TOOLS` names the ones a direct request may reach, and only when it says so.
        """
        if not self.handles(name):
            raise BoardToolError(f"unknown Board tool {name!r}")
        if not isinstance(args, dict):
            raise BoardToolError("Tool arguments must be an object.")
        try:
            if (name in CLEANUP_TOOL_NAMES and self.cleanup is None
                    and not (by_owner and name in OWNER_TOOLS)
                    and not getattr(self.card_scope, "chat", False)):
                raise BoardToolError(f"{name} is only available during a Board cleanup.",
                                     code="board_refused")
            self._check_card_scope(name, args)
            if self.readonly and name in WRITE_TOOLS:
                raise BoardToolError(
                    "This turn writes nothing by design — the owner has not confirmed anything "
                    "yet. Say what you would do; the write happens once the owner answers.",
                    code="board_readonly_turn")
            if name == "board_try" and self.cleanup is not None:
                # Gated exactly as `board_claim` is, and for the same reason: a cleanup is the
                # Board worker tidying files, with no terminal pane of its own to stage a
                # fixture on or open an app in. Preparing a Try it is not tidying.
                raise BoardToolError(
                    "board_try is not available during a Board cleanup: preparing Try it "
                    "stages a fixture and opens it, and a cleanup has no pane of its own to do "
                    "that in. Press Try it on the card, or call this from a terminal pane.",
                    code="board_refused")
            if name == "board_claim" and self.cleanup is not None:
                # A cleanup is the Board worker tidying the whole board (19.9): it has no
                # terminal pane of its own to claim *for*, and putting a card into Executing is
                # not tidying. The ordinary move is still there if a card is genuinely mis-filed.
                raise BoardToolError(
                    "board_claim is not available during a Board cleanup: a cleanup has no "
                    "terminal pane of its own, and taking a card is not tidying one. Move a "
                    "mis-filed card with board_move_card.", code="board_refused")
            if name == "search_files":
                return search_workspace(Path(self.board.repo), dict(args))
            if self.state == "uninitialized" and name not in UNINITIALIZED_TOOLS:
                raise BoardToolError(
                    "This project has no Board yet, so there is nothing to read or change. "
                    "board_create_card is the only board tool here: calling it asks the user "
                    "whether to create one.", code="board_not_initialized")
            if name in WRITE_TOOLS:
                self._check_write_budget(name, args)
                # Last, and only once the budget and the dry run have had their say: a refused
                # call must not open a dialog, and a dry run must not create anything.
                self.ensure_board(title=str(args.get("title") or "")[:200])
            handler = {"board_list": self._list, "board_read": self._read,
                       "board_create_card": self._create, "board_update_card": self._update,
                       "board_move_card": self._move, "board_comment": self._comment,
                       "board_claim": self._claim,
                       "board_merge_cards": self._merge, "board_split_card": self._split,
                       "board_sections": self._sections,
                       "tests_check": self._tests_check, "tests_run": self._tests_run,
                       "board_try": self._board_try,
                       "board_case": self._case,
                       "board_signals": self._signals,
                       "board_import_items": self._import_items}[name]
            return handler(dict(args))
        except BoardToolError as exc:
            # A dry run's refusals are the plan, not a problem: they are already recorded as
            # proposed changes, so they do not also go into the refusal list.
            if self.cleanup is not None and exc.code != "board_cleanup_dry_run":
                self.cleanup.refusals.append({"tool": name, "error": str(exc), "code": exc.code})
            return exc.to_result()
        except B.BoardConflict as exc:
            return {"error": str(exc), "code": "board_conflict", "current_hash": exc.current_hash}
        except B.BoardError as exc:
            if self.cleanup is not None:
                self.cleanup.refusals.append({"tool": name, "error": str(exc), "code": "board_error"})
            return {"error": str(exc), "code": "board_error"}

    def _import_items(self, args: dict) -> dict:
        """`board_import_items`: cards from tracking the project already has (protocol 19.18).

        The same import the page's survey uses (`board_import.propose` + `apply`), so every card
        carries its `source` key and nothing is imported twice.  The keys are re-derived from the
        project here rather than trusted from the caller, exactly as `board_import_apply` does.
        """
        from . import board_import as I
        keys = args.get("keys")
        if (not isinstance(keys, list) or not keys
                or not all(isinstance(k, str) and k.strip() for k in keys)):
            raise BoardToolError("board_import_items needs `keys`: the source keys to import.")
        if len(keys) > 1000:
            raise BoardToolError("board_import_items takes at most 1000 keys.")
        tab = args.get("tab")
        if tab is not None and not (isinstance(tab, str) and tab.strip()):
            raise BoardToolError("board_import_items tab must be a tab id.")
        wanted = list(dict.fromkeys(k.strip() for k in keys))
        try:
            proposals = I.propose(self.board.repo, board=self.board)
            chosen = [p for p in proposals if p.source_key in set(wanted)]
            created = I.apply(self, chosen, tab=tab.strip() if tab else None, actor="agent",
                              emit=self.emit)
        except (I.ImportError_, BoardError, OSError) as exc:
            raise BoardToolError(f"The import could not run: {exc}") from exc
        cards = []
        for card_id in created:
            card = self.board.card_by_id(card_id)
            if card is None:                              # pragma: no cover - deleted mid-import
                continue
            cards.append({"id": card_id, "title": card.title,
                          "source_key": I.source_key_of(card),
                          "path": str(card.path.relative_to(self.board.repo))})
        return {"created": len(cards), "cards": cards,
                "skipped": [k for k in wanted if k not in {c["source_key"] for c in cards}]}

    # ---- limits ---------------------------------------------------------------
    def _check_write_budget(self, name: str, args: dict | None = None) -> None:
        if self.cleanup is not None and self.cleanup.dry_run:
            # A preview run: the plan is recorded, nothing is written.
            self.cleanup.record(CleanupChange(
                action=name.replace("board_", "").replace("_card", "").replace("_cards", ""),
                card_id=str((args or {}).get("id") or (args or {}).get("into") or "").lstrip("#").upper(),
                summary=preview_line(name, args or {}), proposed=True))
            raise BoardToolError(
                "This is a dry run of the Board cleanup: nothing is written. Carry on "
                "reading the board and call the write tools as you would — each call is recorded "
                "as a proposal — then summarize the plan in your reply.",
                code="board_cleanup_dry_run")
        if not self.enforce_limits:
            return
        if self.autonomy == "off":
            raise BoardToolError("Board writes are turned off for this workspace (autonomy: off).",
                                 code="board_autonomy_off")
        if name == "board_create_card":
            if self.creates_this_turn >= self.limit("max_creates_per_turn"):
                raise BoardToolError(
                    f"Board limit: {self.limit('max_creates_per_turn')} new cards per turn. "
                    "Summarize the remaining requests in your reply instead of creating more.",
                    code="board_rate_limited", scope="turn", limit=self.limit("max_creates_per_turn"))
        elif self.writes_this_turn >= self.limit("max_writes_per_turn"):
            raise BoardToolError(
                f"Board limit: {self.limit('max_writes_per_turn')} card writes per turn. "
                "Summarize the rest in your reply.",
                code="board_rate_limited", scope="turn", limit=self.limit("max_writes_per_turn"))

    # ---- reads ----------------------------------------------------------------
    def _tab_map(self) -> dict[str, dict]:
        return {str(t.get("id")): t for t in self.board.tabs() if t.get("id")}

    def _category_for_tab(self, tab: str) -> str:
        # `board.category_for_tab` is the one copy of this: the sync engine writes cards too and
        # has to agree about which folder a tab means. Only the exception type is ours.
        try:
            return B.category_for_tab(self.board, tab, strict=True)
        except B.BoardError as exc:
            raise BoardToolError(str(exc)) from exc

    def _tab_of(self, card: B.Card) -> str:
        return B.tab_of(self.board, card)

    def _require_manual_section(self, section: str) -> None:
        """Refuse unless `section` names a configured column that collects nothing (#3XZV).

        Parking is for the sections a person fills by hand; a column with statuses of its own
        is reached through `status`, and an unconfigured id would park the card nowhere.
        """
        config = self.board.config()
        columns = [str(c) for c in (config.get("columns") or [])]
        if section not in columns:
            raise BoardToolError(
                f"section {section!r} is not one of this board's sections: "
                f"{', '.join(columns) or '(none configured)'}.")
        if B.column_statuses_of(config, section):
            raise BoardToolError(
                f"the {section} section collects statuses, so a card is moved into it with "
                "`status`; `section` parks a card in a section that collects nothing.")

    def _updated_at(self, card: B.Card) -> str | None:
        """When this card last changed on disk: the card file's mtime, or its thread file's if
        that is later (protocol 19.2 ``updated``, 2026-09-19). A git checkout moves mtimes too —
        this says "recently touched", not "recently written by a person", which is what the
        pane's Recently updated sort wants."""
        newest = None
        paths = [card.path] if card.path else []
        if card.id:
            paths.append(self.board.thread_path(card.id, card.private))
        for path in paths:
            try:
                mtime = path.stat().st_mtime
            except OSError:
                continue
            if newest is None or mtime > newest:
                newest = mtime
        if newest is None:
            return None
        return datetime.fromtimestamp(newest, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    def _row(self, card: B.Card, thread_counts: dict[str, int]) -> dict:
        # The full row of protocol 19.2. `created`, the task counts and `milestone` were promised
        # there but never sent, so the pane's age and `☑ done/total` badges had nothing to draw
        # (found while rebuilding the pane as rows, 2026-09-18).
        tasks = card.tasks()
        return {"id": card.id, "title": card.title, "type": card.type, "status": card.status,
                "section": card.front.get("section"),
                "tab": self._tab_of(card), "labels": list(card.front.get("labels") or []),
                "assignee": card.front.get("assignee"), "waiting_on": card.front.get("waiting_on"),
                "rank": card.rank, "private": card.private, "priority": card.priority,
                "path": str(card.path.relative_to(self.board.repo)) if card.path else None,
                "thread_entries": thread_counts.get(card.id or "", 0),
                "created": str(card.front.get("created") or ""),
                "updated": self._updated_at(card),
                "milestone": card.front.get("milestone"),
                "topic": card.front.get("topic"),
                # Owner steer 2026-09-23: rows carry no verify summary — the block is
                # agent-facing in `board_read` — only the deferred note the user sees (#1AA6),
                # as the bare condition ("the pilot runs"), absent otherwise.
                **({"unverified_until": note.removeprefix("unverified until ")}
                    if (note := B.verify_note(card)) else {}),
                "implemented_by": card.front.get("implemented_by"),
                # Who closed it: out of a QA lane (#T71W), or — when this equals `implemented_by` —
                # the pane that both wrote and closed the card, which is what makes it **self-
                # closed** and folds it into one row of the done list (#93WR). Both signatures are
                # on every row so that fold needs no second request. The row stays light otherwise:
                # the `qa` recommendation is computed per card in `board_read`, not for every row.
                "verified_by": card.front.get("verified_by"),
                # Which pane holds the card (#R9G7): the Board draws the token's first eight
                # characters as a link to that pane, and an agent listing the board sees from the
                # row alone that a card is taken.
                "session": card.front.get("session"),
                "tasks_total": len(tasks),
                "tasks_done": sum(1 for task in tasks if task.done)}

    def _threads(self) -> dict[str, list[B.ThreadEntry]]:
        """Every card's thread entries, read once: the count on a row comes from here, and so
        does the searchable text the protocol's own rows carry (19.2 `text`)."""
        out: dict[str, list[B.ThreadEntry]] = {}
        for private in (False, True):
            folder = self.board.threads_dir(private)
            if not folder.is_dir():
                continue
            for path in folder.glob("*.md"):
                try:
                    entries = B.parse_thread(path.read_text(encoding="utf-8"))
                except (OSError, UnicodeDecodeError):
                    continue
                if entries:
                    out[path.stem.upper()] = entries
        return out

    def _thread_counts(self) -> dict[str, int]:
        return {card_id: len(entries) for card_id, entries in self._threads().items()}

    def _list(self, args: dict) -> dict:
        allowed = {"tab", "status", "type", "labels", "query", "limit", "cases", "server", "card"}
        if set(args) - allowed:
            raise BoardToolError(f"board_list takes {', '.join(sorted(allowed))}.")
        limit = args.get("limit", 25)
        if not isinstance(limit, int) or not 1 <= limit <= MAX_LIST_LIMIT:
            raise BoardToolError(f"limit must be an integer from 1 to {MAX_LIST_LIMIT}.")
        if args.get("cases"):
            return self._list_cases(args, limit)
        if args.get("server") is not None or args.get("card") is not None:
            raise BoardToolError("server and card filter the case ledger: pass cases: true.")
        want_type = args.get("type")
        if want_type is not None and want_type not in B.CARD_TYPES:
            raise BoardToolError(f"type must be one of {', '.join(B.CARD_TYPES)}.")
        want_labels = {str(l).lower() for l in (args.get("labels") or [])}
        query = str(args.get("query") or "").strip().lower()
        want_tab = str(args.get("tab")).strip().lower() if args.get("tab") else None
        want_status = str(args.get("status")).strip().lower() if args.get("status") else None
        if want_tab:
            entry = self._tab_map().get(want_tab)
            if entry is None:
                raise BoardToolError(f"unknown tab {args['tab']!r}.")
        counts = self._thread_counts()
        rows = []
        for card in self.board.cards():
            if card.id is None:
                continue
            if want_type is not None and card.type != want_type:
                continue
            if want_type is None and args.get("tab") is None and card.type != "work":
                continue
            if want_status and card.status != want_status:
                continue
            if want_tab and self._tab_of(card) != want_tab:
                continue
            if want_labels and not want_labels <= {str(l).lower() for l in (card.front.get("labels") or [])}:
                continue
            if query and query not in f"{card.id} {card.title} {card.body}".lower():
                continue
            rows.append(card)
        rows.sort(key=lambda c: (B._status_order(c), c.rank or "zzzz", str(c.path)))
        out = [self._row(c, counts) for c in rows[:limit]]
        return {"cards": out, "total": len(rows), "truncated": len(rows) > limit,
                "tabs": [t for t in self._tab_map()], "autonomy": self.autonomy}

    def _card(self, card_id: str) -> B.Card:
        card = self.board.card_by_id(card_id)
        if card is None:
            raise BoardToolError(f"no card #{card_id} on this board.", code="board_not_found")
        return card

    def _read(self, args: dict) -> dict:
        if set(args) - {"id", "thread_entries"}:
            raise BoardToolError("board_read takes id and thread_entries.")
        card_id = normalize_id(args.get("id"))
        count = args.get("thread_entries", 20)
        if not isinstance(count, int) or not 0 <= count <= MAX_THREAD_ENTRIES:
            raise BoardToolError(f"thread_entries must be an integer from 0 to {MAX_THREAD_ENTRIES}.")
        card = self._card(card_id)
        entries = self.board.thread(card_id, card.private)
        entries.sort(key=lambda e: e.entry_id)
        tail = entries[len(entries) - count:] if count else []
        qa = self._qa_block(card)
        # The `verify` block (#WFRA) reaches the GUI under `front`, normalized (`also` a list,
        # `human` and `sign_off` filled) so the card page's strip reads one shape; a block that
        # does not validate is left as written and named in `verify_error`.
        front = dict(card.front)
        verify_error = ""
        if front.get("verify") is not None:
            try:
                front["verify"] = B.validate_verify(front["verify"])
            except B.BoardError as exc:
                verify_error = str(exc)
        return {**({"qa": qa} if qa else {}),
                **({"verify_error": verify_error} if verify_error else {}),
                "id": card.id, "hash": B.file_hash(card.path), "type": card.type,
                "tab": self._tab_of(card), "status": card.status,
                "path": str(card.path.relative_to(self.board.repo)),
                "front": front, "title": card.title, "body": card.body[:MAX_TEXT],
                "body_truncated": len(card.body) > MAX_TEXT,
                "sections": section_headings(card.body),
                # The user's own words, so the pane can offer them for editing without parsing
                # Markdown itself.  `issue_heading` is the spelling this card uses today
                # (`Request` on a card written before 2026-09-18); a write settles it on `Issue`.
                "issue": _section_text(card.body, B.ISSUE_HEADING).strip("\n"),
                "issue_heading": next((h for h in section_headings(card.body)
                                       if _heading_matches(h, B.ISSUE_HEADING)), B.ISSUE_HEADING),
                "tasks": [{"item_id": t.item_id, "text": t.text, "status": t.status,
                           "done": t.done, "depth": t.depth, "card": t.card,
                           "blocked_by": list(t.blocked_by)}
                          for t in card.tasks()],
                # The QA policy floor this board works under (#C3Q2), agent-facing.
                "qa_policy": QP.effective_line(self.qa_policy),
                "thread_total": len(entries),
                "thread": [{"entry_id": e.entry_id, "author": e.author, "kind": e.kind,
                            "attrs": dict(e.attrs), "text": e.text} for e in tail]}

    def _qa_block(self, card: B.Card) -> dict | None:
        """The `qa` object of protocol 19.15: who should verify this card, and what its commits say.

        Only on a work card that names an implementer — with nothing to be independent *of* there is
        nothing to recommend. Availability is this machine's (`qa_verifiers.availability`, cached a
        minute because it shells out per stored key), never the GUI's, and the commit trailers are
        read from `links.commits` plus `git log --grep '#ID'`. Advisory throughout: a missing git, a
        bad hash or an unreadable keyring answers with fewer rows, never an error.
        """
        if card.type != "work":
            return None
        implementer = str(card.front.get("implemented_by") or "").strip()
        if not implementer:
            return None
        block = QA.recommend_here(implementer)
        block["commits"] = QA.card_commits(self.board.repo, card.id or "",
                                           card.front.get("links"), implementer)
        verified = str(card.front.get("verified_by") or "").strip()
        if verified:
            block["verified_by"] = verified
            block["verifier_family"] = QA.family(verified)
        return block

    def _qa_floor(self, card: B.Card, verify: dict) -> tuple[dict, list[str]]:
        """`qa_policy.apply` over one normalized block for this card (#C3Q2): the lineage rule
        reads the card's `qa` recommendation (protocol 19.15) without its commit list."""
        implementer = str(card.front.get("implemented_by") or "").strip()
        qa = QA.recommend_here(implementer) if implementer else None
        return QP.apply(self.qa_policy, verify, qa, cases=self._verified_cases(card))

    def _apply_qa_floor(self, card: B.Card) -> list[str]:
        """Run the floor over the `verify` block already on the card, in place, and return the
        notes (agent-facing only: they ride in the tool result, never in a thread event or on a
        row).  A card without a readable block is left alone: `fields.verify` is the way to fix
        one, and the floor does not guess at what it cannot read."""
        raw = card.front.get("verify") if card.type == "work" else None
        if raw is None:
            return []
        try:
            verify = B.validate_verify(raw)
        except B.BoardError:
            return []
        floored, notes = self._qa_floor(card, verify)
        if notes:
            card.set("verify", floored)
        return notes

    def _qa_ask_gate(self, card: B.Card, old_status: str, status: str) -> str:
        """The one switch a user sees of the floor (#C3Q2, Options › Agent › QA › Verification).

        A verifying session — one closing a card out of `needs-verification` or a QA lane —
        may move a card whose plan needs no person (`verify.human` not `required`) to `done`
        only under `verification: automatic`; under `ask` (the default) the card is the user's
        to close and the move is refused in one sentence offering `needs-verification`.  Returns
        the one-line note for the result when the close is automatic, "" otherwise.  A card
        with no `verify` block, the owner's own close, a self-close out of `executing` (the
        medium tier) and a card whose person has answered are not this gate's.
        """
        if status != "done" or self.context.actor == OWNER_ACTOR:
            return ""
        if old_status not in QA_STATUSES and old_status != "needs-verification":
            return ""
        try:
            verify = B.verify_block(card)
        except B.BoardError:
            return ""
        if verify is None or verify.get("human") == "required":
            return ""
        if QP.closes_automatically(self.qa_policy, verify):
            return ("closed automatically: Verification is automatic and the plan needs no "
                    f"person (verify.human: {verify.get('human', 'none')})")
        raise BoardToolError(
            f"#{card.id} is the user's to close (Verification: ask me before closing any card), "
            "so leave it in needs-verification with the evidence and they move it to done.",
            code="board_refused", requires="user_close", offer="needs-verification",
            id=card.id or "")

    # ---- writes ---------------------------------------------------------------
    def _record(self, action: str, card: B.Card, summary: str, before: bytes | None,
                thread_size: int, moved_from: Path | None = None,
                others: Sequence[tuple[Path, bytes | None, Path | None]] = (),
                cards: Sequence[str] = (), removed: bool = False) -> str:
        return self._record_path(action, card.path, card.id or "", summary, before,
                                 self.board.thread_path(card.id or "", card.private),
                                 thread_size, moved_from, others, cards, removed)

    def _record_path(self, action: str, path: Path, card_id: str, summary: str,
                     before: bytes | None, thread_path: Path | None, thread_size: int,
                     moved_from: Path | None = None,
                     others: Sequence[tuple[Path, bytes | None, Path | None]] = (),
                     cards: Sequence[str] = (), removed: bool = False) -> str:
        """Record one undoable write, announce it, and (in a cleanup) log it for the changelog.

        `removed` is the one write that takes a card off the board rather than changing it
        (the owner's delete): the change event names the card in `removed`, not `upserts`,
        so the panes drop the row instead of asking for a card that is no longer there.
        """
        write_id = f"w-{int(self.clock() * 1000):x}-{secrets.token_hex(2)}"
        record = WriteRecord(write_id=write_id, action=action, card_id=card_id,
                             path=path, summary=summary, at=self.clock(), before=before,
                             thread_path=thread_path, thread_size=thread_size,
                             moved_from=moved_from, others=list(others))
        self.writes[write_id] = record
        self._order.append(write_id)
        if card_id and card_id not in self.cards_this_turn:
            self.cards_this_turn.append(card_id)
        while len(self._order) > 50:
            self.writes.pop(self._order.pop(0), None)
        rel = str(path.relative_to(self.board.repo))
        activity = {"event": "board_activity", "write_id": write_id, "id": card_id or None,
                    "action": action, "actor": self.context.actor, "model": self.context.model,
                    "pane": self.context.pane, "turn_id": self.context.turn_id,
                    "summary": summary, "path": rel, "undo_seconds": UNDO_SECONDS}
        if self.cleanup is not None:
            activity["cleanup"] = True
            activity["run_id"] = self.cleanup.run_id
            self.cleanup.record(CleanupChange(action=action, card_id=card_id, summary=summary,
                                              path=rel, write_id=write_id, cards=list(cards)))
        self._emit_board(activity)
        self._emit_board({"event": "board_changed",
                          "upserts": [] if removed else ([card_id] if card_id else []),
                          "removed": [card_id] if removed else [], "write_id": write_id})
        return write_id

    def _thread_size(self, card: B.Card) -> int:
        path = self.board.thread_path(card.id or "", card.private)
        try:
            return path.stat().st_size
        except OSError:
            return 0

    def _append(self, card: B.Card, text: str, kind: str = "event", **attrs) -> B.ThreadEntry:
        return self.board.append_thread(card.id, text, author=self.context.actor, kind=kind,
                                        private=card.private, **self.context.attrs(), **attrs)

    def _create(self, args: dict) -> dict:
        allowed = {"tab", "status", "section", "title", "request", "type", "labels", "source",
                   "related", "not_duplicate_of"}
        if set(args) - allowed:
            raise BoardToolError(f"board_create_card takes {', '.join(sorted(allowed))}.")
        card_type = args.get("type") or "work"
        if card_type not in B.CARD_TYPES:
            raise BoardToolError(f"type must be one of {', '.join(B.CARD_TYPES)}.")
        title = _one_line(args.get("title"), "title", MAX_TITLE)
        request = _text(args.get("request"), "request", MAX_REQUEST)
        status = str(args.get("status") or "").strip().lower()
        if status not in B.STATUS_FOLDER[card_type]:
            raise BoardToolError(f"unknown {card_type} status {args.get('status')!r}; use one of "
                                 f"{', '.join(B.STATUS_FOLDER[card_type])}.")
        category = (B.MEMORY_FOLDER if card_type == "memory" else
                    B.ALIAS_FOLDER if card_type == "alias" else self._category_for_tab(args.get("tab")))
        labels = _string_list(args.get("labels"), "labels")
        related = [normalize_id(r, "related") for r in _string_list(args.get("related"), "related")]
        section = str(args.get("section") or "").strip().lower()
        if section:
            self._require_manual_section(section)

        cards = self.board.cards()
        excused = {normalize_id(r, "not_duplicate_of") for r in _string_list(args.get("not_duplicate_of"), "not_duplicate_of")}
        duplicates = self.duplicates(title, request, cards, excused) if self.duplicate_check else []
        if duplicates:
            raise BoardToolError(
                "This looks like a card that already exists. Read it and update it instead, or repeat "
                "the call with not_duplicate_of listing the ids you checked.",
                code="board_possible_duplicate", possible_duplicates=duplicates)

        if self.enforce_limits and not self.rate.claim(self.limit("max_creates_per_hour")):
            raise BoardToolError(
                f"Board limit: {self.limit('max_creates_per_hour')} new cards per hour for this "
                "workspace. Summarize the remaining requests in your reply.",
                code="board_rate_limited", scope="hour", limit=self.limit("max_creates_per_hour"))

        taken = [c.id for c in cards if c.id]
        card = B.new_card(card_type, title, status, card_id=B.new_id(taken), request=request,
                          rank=self.board.next_rank([c for c in cards if c.status == status]),
                          labels=labels or None, source=args.get("source") or None)
        if related:
            links = dict(card.front.get("links") or {})
            links["related"] = related
            card.set("links", links)
        if section:
            card.set("section", section)
        try:
            path = B.write_new_card(self.board, card, category)
        except B.BoardError as exc:
            raise BoardToolError(str(exc)) from exc

        self.creates_this_turn += 1
        self.writes_this_turn += 1
        rel = str(path.relative_to(self.board.repo))
        size = self._thread_size(card)
        self._append(card, f"- ✦ {self.context.actor} created this card in "
                           f"{_column_label(status)} · {rel}", kind="event")
        summary = f"created · {_column_label(status)}"
        write_id = self._record("create", card, summary, None, size)
        return {"id": card.id, "path": rel, "status": status, "section": section or None,
                "tab": self._tab_of(card),
                "hash": B.file_hash(path), "write_id": write_id, "created": True}

    def duplicates(self, title: str, request: str, cards: Sequence[B.Card] | None = None,
                   excused: set[str] = frozenset(), threshold: float = 0.66) -> list[dict]:
        """Cards whose title or request looks like this one (open cards only)."""
        out = []
        for card in (cards if cards is not None else self.board.cards()):
            if card.id is None or card.id in excused or card.status in ("done", "dropped"):
                continue
            body_request = ""
            span = _section_span(card.body, B.ISSUE_HEADING)
            if span:
                body_request = card.body[span[1]:span[2]]
            score = max(similarity(title, card.title), similarity(request, body_request),
                        similarity(title, body_request))
            if score >= threshold:
                out.append({"id": card.id, "title": card.title, "status": card.status,
                            "score": round(score, 3),
                            "path": str(card.path.relative_to(self.board.repo)) if card.path else None})
        return sorted(out, key=lambda d: -d["score"])[:5]

    def _verify_from_skills(self, card: B.Card) -> tuple[dict | None, str | None, str | None]:
        """The `verify` block a work card with none inherits from the skill this turn loaded (#MSJ0).

        Returns `(block, skill_id, note)`.  Exactly one loaded skill with a profile carrying
        verify keys gives the block — `PROFILE_VERIFY_KEYS` only, validated like an explicit
        `fields.verify` — and a note saying so.  Two or more profiled skills are not guessed
        between: no block, and the note names them so the agent sets `fields.verify` itself.
        A profile that does not validate as a block (say `human: required` with no `criteria`)
        is reported the same way rather than written.  Agent-facing only (owner steer
        2026-09-23): the note rides in the tool result and the thread event, nowhere a user reads.
        """
        if card.type != "work" or card.front.get("verify") is not None:
            return None, None, None
        profiled = {sid: {k: prof[k] for k in PROFILE_VERIFY_KEYS if k in prof}
                    for sid, prof in self.context.skills.items()
                    if isinstance(prof, dict) and any(k in prof for k in PROFILE_VERIFY_KEYS)}
        if not profiled:
            return None, None, None
        if len(profiled) > 1:
            names = ", ".join(sorted(profiled))
            return None, None, (f"verify not defaulted: skills {names} each carry a profile; "
                                "set fields.verify yourself")
        (sid, block), = profiled.items()
        try:
            return B.validate_verify(block), sid, f"verify defaulted from skill {sid}"
        except B.BoardError as exc:
            return None, None, f"verify not defaulted from skill {sid}: {exc}"

    def _update(self, args: dict) -> dict:
        allowed = {"id", "base_hash", "fields", "title", "append_section", "replace_section",
                   "replace_agent_section", "tasks"}
        if set(args) - allowed:
            raise BoardToolError(f"board_update_card takes {', '.join(sorted(allowed))}.")
        card_id = normalize_id(args.get("id"))
        base_hash = args.get("base_hash")
        if not isinstance(base_hash, str) or len(base_hash) != 64:
            raise BoardToolError("base_hash must be the 64-character `hash` returned by board_read.")
        card = self._card(card_id)
        before = card.path.read_bytes()
        # The QA policy floor (#C3Q2): what `verify` was when this write began, and the notes
        # the floor leaves for the agent when it changes the block this write puts on the card.
        verify_before, qa_notes = card.front.get("verify"), []
        current = B.file_hash(card.path)
        if current != base_hash:
            # The hash is named in the message as well as the fields: a model that mistyped or
            # invented one repeated the same wrong hash four times in a live cleanup, because the
            # sentence did not say what the right one was (2026-09-18).
            raise BoardToolError(f"#{card_id} does not have the base_hash you sent; it is now "
                                 f"{current}. Read it again and reapply your change to what you "
                                 "read back.", code="board_conflict", current_hash=current, id=card_id)

        changes: list[str] = []
        rewrites: list[tuple[str, str, str]] = []   # (what, old, new)
        verdict_text: str | None = None              # a `## Verdict` this write put on the card

        fields = args.get("fields")
        if fields is not None:
            if not isinstance(fields, dict):
                raise BoardToolError("fields must be an object of front matter keys.")
            writable = B.ALLOWED_FIELDS[card.type] - IMMUTABLE_FIELDS
            for key, value in fields.items():
                if key in IMMUTABLE_FIELDS:
                    raise BoardToolError(
                        f"{key} is not writable: id, type and created are the record, source is the "
                        "user's own provenance, private moves the file, and status, rank and "
                        "section are board_move_card's job.", code="board_refused", field=key)
                if key not in writable:
                    raise BoardToolError(f"{key} is not a field of a {card.type} card; allowed: "
                                         f"{', '.join(sorted(writable))}.", code="board_refused", field=key)
                if key == "priority" and value is not None:
                    # One clamped int (#VKFV): a bad value is refused rather than guessed at,
                    # and 0 means "no flag", which is the key's absence in the file.
                    try:
                        value = B.clamp_priority(value)
                    except B.BoardError as exc:
                        raise BoardToolError(str(exc), code="board_refused", field=key) from exc
                    if value == 0:
                        value = None
                if key == "verify" and value is not None:
                    # The QA ladder's block (#WFRA): validated against the vocabulary, refused
                    # naming the bad key or value, stored normalized.
                    try:
                        value = B.validate_verify(value)
                    except B.BoardError as exc:
                        raise BoardToolError(f"{exc}.", code="board_refused", field=key) from exc
                    # Floored before it is stored (#C3Q2), so the change line names the block
                    # that stands; the notes say what the floor did.
                    value, qa_notes = self._qa_floor(card, value)
                old = card.front.get(key)
                if value is None:
                    card.drop(key)
                else:
                    card.set(key, value)
                if old != card.front.get(key):
                    changes.append(f"{key}: {_short(old)} → {_short(card.front.get(key))}")
                if key == "verify" and isinstance(old, dict) and old.get("deferred") \
                        and not (card.front.get(key) or {}).get("deferred"):
                    # The only way on from a deferred card (#1AA6), so the thread says who took
                    # it: the event this update leaves carries the actor, model and pane.
                    changes.append(f"verify.deferred cleared by {self.context.actor} "
                                   f"(was {_short(old.get('deferred'))})")

        if args.get("title") is not None:
            new_title = _one_line(args.get("title"), "title", MAX_TITLE)
            old_title = card.title
            if new_title != old_title:
                match = _H1_RE.search(card.body)
                if match is None:
                    raise BoardToolError("this card has no `# ` heading to replace.")
                card.body = card.body[:match.start()] + f"# {new_title}" + card.body[match.end():]
                changes.append(f"title: {_short(old_title)} → {_short(new_title)}")
                rewrites.append(("title", old_title, new_title))

        for key in ("append_section", "replace_section", "replace_agent_section"):
            block = args.get(key)
            if block is None:
                continue
            if not isinstance(block, dict) or set(block) - {"heading", "text"}:
                raise BoardToolError(f"{key} takes {{heading, text}}.")
            heading = _one_line(block.get("heading"), "heading", 120)
            # One spelling for the section that holds the user's own words, whichever the caller
            # used: a card written as `## Request` comes out saying `## Issue` once it is edited.
            if heading.strip().lower() in B.ISSUE_HEADINGS:
                heading = B.ISSUE_HEADING
            text = _without_own_heading(_text(block.get("text"), "text", MAX_SECTION), heading)
            replace = key != "append_section"
            old_text = _section_text(card.body, heading)
            card.body = _write_section(card.body, heading, text, replace=replace)
            card.dirty = True
            verb = "replaced" if replace else "appended to"
            changes.append(f"{verb} `## {heading}`")
            if replace and old_text.strip() and is_owner_section(heading):
                rewrites.append((f"## {heading}", old_text, text))
            if heading.strip().lower() in ("verdict", "qa verdict", "qa result"):
                verdict_text = text

        if args.get("tasks") is not None:
            items = _task_items(args["tasks"], card)
            card.write_tasks(items)
            card.dirty = True
            changes.append(f"tasks: {sum(1 for i in items if i.done)}/{len(items)} done")

        if not changes:
            raise BoardToolError("nothing to change: pass fields, title, append_section, "
                                 "replace_section or tasks.")

        # A card still without a `verify` block inherits the loaded skill's (#MSJ0).  An
        # explicit `fields.verify` — even one that cleared it — always wins and says nothing.
        defaulted_from = verify_note = None
        if "verify" not in (fields or {}):
            block, defaulted_from, verify_note = self._verify_from_skills(card)
            if block is not None:
                card.set("verify", block)
                changes.append(verify_note)

        # A block this write put on the card by another route than `fields.verify` (a loaded
        # skill's default) meets the same floor (#C3Q2).
        if not qa_notes and card.front.get("verify") is not None \
                and card.front.get("verify") != verify_before:
            qa_notes = self._apply_qa_floor(card)
        size = self._thread_size(card)
        self.board.save(card, base_hash=base_hash)
        self.writes_this_turn += 1
        summary = "; ".join(changes)[:400]
        self._append(card, f"- ✦ {self.context.actor} updated this card · {summary}", kind="event")
        # Decision 12.3: a rewrite of the user's own text is allowed, and the thread keeps the old
        # and the new text so the discussion history shows it and it can be put back.
        for what, old, new in rewrites:
            self._append(card, _rewrite_entry(what, old, new), kind="rewrite")
        write_id = self._record("update", card, summary, before, size)
        # A verifier's `## Verdict` is a case decided (#95VZ): one ledger row naming the server
        # that served the card, with the verdict's first decisive word as its result.
        case = None
        if verdict_text is not None:
            case = self.record_case(card=card.id, verdict=CASES.verdict_from_text(verdict_text),
                                    signal={"mode": _primary_mode(card),
                                            "result": CASES.verdict_from_text(verdict_text)},
                                    input=f"card #{card.id}")
        return {"id": card.id, "hash": B.file_hash(card.path), "changes": changes,
                "write_id": write_id, "logged_rewrites": [w for w, _, _ in rewrites],
                **({"case": case["id"]} if case else {}),
                **({"qa_policy": qa_notes} if qa_notes else {}),
                **({"verify_defaulted_from": defaulted_from} if defaulted_from else {}),
                **({"note": verify_note} if verify_note else {})}

    def _move(self, args: dict) -> dict:
        allowed = {"id", "status", "section", "tab", "before", "after", "reason", "evidence",
                   "implemented_by"}
        if set(args) - allowed:
            raise BoardToolError(f"board_move_card takes {', '.join(sorted(allowed))}.")
        card_id = normalize_id(args.get("id"))
        reason = _one_line(args.get("reason"), "reason", MAX_REASON)
        card = self._card(card_id)
        before_bytes = card.path.read_bytes()
        base_hash = B.file_hash(card.path)
        old_status, old_tab = card.status, self._tab_of(card)
        old_section = str(card.front.get("section") or "")
        status = str(args.get("status")).strip().lower() if args.get("status") else old_status
        if status not in B.STATUS_FOLDER[card.type]:
            raise BoardToolError(f"unknown {card.type} status {args.get('status')!r}; use one of "
                                 f"{', '.join(B.STATUS_FOLDER[card.type])}.")
        # Before anything is written: an open signal this pane's own runs opened stops the
        # card landing (#AQ6X decision 6). It is refused, not warned about, because the machine
        # state wins over the card's — and it is checked here, where every status change of every
        # write path already funnels through.
        self._signal_gate(card, old_status, status)
        # And a judgement the person has not made yet stops the card closing (#WC3E): an
        # unanswered `## Human QA` question is exactly the case where evidence is not the answer.
        self._human_qa_gate(card, status)
        # And the card's own `verify` block (#1AA6): deferred verification holds it out of
        # `done` and the QA lanes; `human: required` needs the person's answer; a sign-off
        # needs its receipt.
        self._verify_gate(card, status)
        # And the one switch (#C3Q2): under `ask` a verifier does not close a card whose plan
        # needs no person; under `automatic` it does, and the result says so in one line.
        qa_note = self._qa_ask_gate(card, old_status, status)
        # `section` parks the card in a manual section — a column that collects nothing — and an
        # empty string takes it out (#3XZV). Its status is left alone either way.
        section_arg = args.get("section")
        if section_arg is not None:
            if not isinstance(section_arg, str):
                raise BoardToolError("section must be a section id, or an empty string to take "
                                     "the card out of one.")
            if section_arg.strip():
                self._require_manual_section(section_arg.strip().lower())
                section_arg = section_arg.strip().lower()
            else:
                section_arg = ""
        new_section = old_section if section_arg is None else section_arg
        tab = str(args.get("tab")).strip().lower() if args.get("tab") else old_tab
        # A card of a type with a folder of its own ignores the tab: #W3KD, an alias card could
        # not be retired at all because `aliases` is not a configured tab and the lookup is strict.
        category = (B.MEMORY_FOLDER if card.type == "memory" else B.ALIAS_FOLDER if card.type == "alias"
                    else self._category_for_tab(tab))

        evidence = args.get("evidence")
        # Card #T71W: the worker knows its own preset and model, so the signature is stamped, never
        # typed. It wins over the agent's `implemented_by` argument, which stays for the one case
        # the worker cannot know — a guest CLI writing through the bridge.
        mine = self.context.signature()
        existing_implemented_by = str(card.front.get("implemented_by") or "").strip()
        stamped = ""
        if mine and (status in ("in-progress", "executing", "needs-verification")
                     or (status in QA_STATUSES and old_status not in QA_STATUSES
                         and not existing_implemented_by)):
            stamped = mine
            card.set("implemented_by", mine)
        if status in QA_STATUSES and old_status not in QA_STATUSES:
            if not isinstance(evidence, str) or not evidence.strip():
                raise BoardToolError(
                    "Moving a card into a QA lane needs `evidence`: the path of the evidence folder "
                    "(docs/qa_evidence/<date>-<slug>/) recorded with the change.",
                    code="board_refused", requires="evidence")
            implemented_by = stamped or card.front.get("implemented_by") or args.get("implemented_by")
            if not implemented_by:
                raise BoardToolError(
                    "Moving a card into a QA lane needs `implemented_by`: the model that implemented "
                    "it, so QA can be run by a different one.",
                    code="board_refused", requires="implemented_by")
            card.set("implemented_by", implemented_by)
        verified = ""
        if old_status in QA_STATUSES and status in ("done", "dropped"):
            # A verdict only (#Z4HR): `## Resolution` records how a closed card was removed,
            # not that a verifier checked it, so it no longer satisfies this gate.
            if not any(h.lower() in ("verdict", "qa verdict", "qa result")
                       for h in section_headings(card.body)):
                raise BoardToolError(
                    "A card in a QA lane is closed with a verdict: add a `## Verdict` section "
                    "to the body first, then move it. `## Resolution` is not a verdict "
                    "(#Z4HR): it records how a closed card was removed.",
                    code="board_refused", requires="verdict")
            if QA.is_relay_free(mine):
                raise BoardToolError(
                    "Verifying is not available on Relay Free (owner's decision, 2026-09-19): "
                    "run QA on a provider key, on Codex or on Claude Code, and close it from there.",
                    code="board_refused", requires="independent_model")
            # Any pane may close it once the verdict is there (owner, 2026-09-20, card #76DJ):
            # the gate is the verifier's verdict on the card, not the closer's model family.
            # Who passed it, in the same canonical form as `implemented_by`.
            if status == "done" and mine:
                verified = mine
                card.set("verified_by", mine)
        elif status == "done" and mine and self.context.actor != OWNER_ACTOR:
            # A **self-close** (#93WR; the *medium* tier of `board_policy.md` v3, where a card the
            # agent sized as medium goes straight to `done`). The card never entered a QA lane —
            # the branch above owns that path — so the pane that did the work is also the only
            # thing that checked it, and `verified_by` records exactly that: the closer's own
            # signature, in the same canonical form as `implemented_by`.
            #
            # **Self-closed is `verified_by` == `implemented_by`, and nothing else.** That equality
            # is the whole marker the done lists fold on, so the two stamps must be the same
            # string when and only when one pane both wrote and closed the card. A card the agent
            # created and closed inside one stretch of work has no implementer yet (nothing moved
            # it through `executing`), so it gets one here too; a card somebody else implemented
            # keeps *their* signature and only gains a `verified_by`, the two differ, and it stays
            # an ordinary done card — which is the point: a cross-pane close is not a self-close.
            #
            # The owner's hand-close from the Board is never stamped, so it never folds.
            # Two independent things say it is the owner, and either alone would do: the
            # owner-side tools are built with `actor="owner"` (`board_protocol._build`) and, unlike
            # the agent's, never learn a preset or a model — only `Agent.sign_board` sets those, on
            # the agent's own instance — so `mine` is "" on that path anyway. The actor is checked
            # as well so that an owner-side context which one day does know its model cannot start
            # stamping the owner's closes by accident. A guest writing through the bridge has no
            # signature either and is likewise left alone.
            #
            # `dropped` verifies nothing — the work was abandoned, not shipped — so it is left
            # unstamped here exactly as it is in the QA branch above.
            verified = mine
            card.set("verified_by", mine)
            if not str(card.front.get("implemented_by") or "").strip():
                stamped = mine
                card.set("implemented_by", mine)

        if args.get("evidence"):
            links = dict(card.front.get("links") or {})
            paths = list(links.get("evidence") or [])
            if evidence not in paths:
                paths.append(evidence)
            links["evidence"] = paths
            card.set("links", links)
        if args.get("implemented_by") and not stamped and not existing_implemented_by:
            card.set("implemented_by", args["implemented_by"])
        if section_arg == "":
            card.drop("section")
        elif section_arg is not None:
            card.set("section", section_arg)

        card.set("status", status)
        # Auto-release (#R9G7, owner 2026-09-20): a card that lands in `done` or `dropped` drops
        # its claim in the same write. This is the one place a status change goes through — the
        # `board_move_card` tool, the Board's `board_move` message (19.3) and the close out
        # of a QA lane above are all here — so no closing path can forget it.
        released = self._release_on_close(card, status)
        rank = self._rank_for(card, status, args.get("before"), args.get("after"))
        if rank is not None:
            card.set("rank", rank)

        target = B.card_target_path(self.board, card, category)
        moved_from = card.path if target is not None else None
        if target is not None and target.exists():
            raise BoardToolError(f"a different file already sits at {target.relative_to(self.board.repo)}.")
        size = self._thread_size(card)
        self.board.save(card, base_hash=base_hash)
        if target is not None:
            B.move_card_file(card, target)

        self.writes_this_turn += 1
        parts = []
        if status != old_status:
            parts.append(f"{_column_label(old_status)} → {_column_label(status)}")
        if new_section != old_section:
            parts.append(f"parked in {_column_label(new_section)}" if new_section
                         else f"out of {_column_label(old_section)}")
        if tab != old_tab:
            parts.append(f"tab {old_tab} → {tab}")
        if rank is not None and not parts:
            parts.append("reordered")
        if released:
            parts.append(f"session {released[:8]} released")
        summary = ", ".join(parts) or "unchanged"
        line = f"- ✦ {self.context.actor} moved this card · {summary} · {reason}"
        if args.get("evidence"):
            line += f" · evidence {evidence}"
        if stamped:
            line += f" · implemented_by {stamped}"
        if verified:
            line += f" · verified_by {verified}"
        self._append(card, line, kind="event")
        write_id = self._record("move", card, summary, before_bytes, size, moved_from)
        # The ledger (#95VZ): `done` is the case passed; a card sent back a stage from
        # verification, a QA lane or `done` is the case failed.  Other moves are not verdicts.
        case = None
        if card.type == "work" and status != old_status:
            back = (old_status in (*QA_STATUSES, "needs-verification", "needs-review", "done")
                    and _status_rank(status) < _status_rank(old_status)
                    and status not in ("done", "dropped"))
            if status == "done" or back:
                case = self.record_case(card=card.id, verdict="pass" if status == "done" else "fail",
                                        input=f"card #{card.id}",
                                        signal={"mode": _primary_mode(card),
                                                "result": "pass" if status == "done" else "fail"})
        return {"id": card.id, "status": status, "section": new_section or None, "tab": tab,
                "rank": card.rank,
                "path": str(card.path.relative_to(self.board.repo)),
                "hash": B.file_hash(card.path), "moved": moved_from is not None,
                "write_id": write_id, "summary": summary,
                **({"case": case["id"]} if case else {}),
                **({"qa_policy": qa_note} if qa_note else {})}

    def set_priority(self, card_id: str, priority) -> dict:
        """The pane's flag click (protocol 19.3 ``board_priority``, card #VKFV).

        Not a `run()` tool: it is the owner at the keyboard, not an agent turn, so it takes no
        `base_hash` — the whole patch is one clamped integer, like a drag's rank — and it is not
        offered to the agent, which sets the same field through `board_update_card`. Undo, the
        write record and the thread entry are the ordinary ones, so a misclick is Ctrl+Z like
        any other move.
        """
        card_id = normalize_id(card_id)
        priority = B.clamp_priority(priority)   # raises BoardError -> BoardToolError below
        card = self._card(card_id)
        before = card.path.read_bytes()
        base_hash = B.file_hash(card.path)
        old_priority = card.priority
        if priority:
            card.set("priority", priority)
        else:
            card.drop("priority")
        size = self._thread_size(card)
        self.board.save(card, base_hash=base_hash)
        self.writes_this_turn += 1
        flag = ("-" if priority < 0 else "+" if priority else "") + str(abs(priority))
        line = (f"- ✦ {self.context.actor} flagged this card · priority {flag}"
                if priority else f"- ✦ {self.context.actor} cleared this card's priority flag")
        self._append(card, line, kind="event")
        summary = f"priority {old_priority} → {priority}"
        write_id = self._record("priority", card, summary, before, size)
        return {"id": card.id, "priority": priority, "hash": B.file_hash(card.path),
                "write_id": write_id, "summary": summary}

    def delete_card(self, card_id: str, reason: str = "") -> dict:
        """The owner's confirmed delete (protocol 19.3 ``board_delete``, card #CYM9).

        Not a `run()` tool, like `set_priority` above it: an agent closes a card by moving it to
        `done` or `dropped` (`board_policy.md`, "Nothing is deleted"), and this is the owner at the keyboard
        asking for the file to go — the same standing as editing the card file by hand.  The
        card file and its thread are unlinked from disk, and the bytes ride the write record
        like any other write, so the GUI's Undo puts both back for `UNDO_SECONDS`; after that
        git is the only recovery, and only for a card it has seen.

        Nothing is appended to the thread — there is no card to append to.  Undo therefore
        restores the thread byte-for-byte as it was, without even the usual "undid" line.
        """
        card = self._card(normalize_id(card_id))
        reason = " ".join(str(reason or "").split())[:MAX_REASON]
        before_bytes = card.path.read_bytes()
        # Both privacy variants of the thread: a card flipped private and back can leave the
        # older file behind, and a delete that left half a card on disk would not be a delete.
        threads = [(path, path.read_bytes())
                   for path in (self.board.thread_path(card.id, card.private),
                                self.board.thread_path(card.id, not card.private))
                   if path.exists()]
        rel = str(card.path.relative_to(self.board.repo))
        card.path.unlink()
        for path, _ in threads:
            path.unlink()
        self.writes_this_turn += 1
        summary = f"deleted · {reason}" if reason else "deleted"
        write_id = self._record("delete", card, summary, before_bytes, 0,
                                others=[(path, content, None) for path, content in threads],
                                removed=True)
        return {"id": card.id, "removed": True, "path": rel,
                "write_id": write_id, "summary": summary}

    #: True inside `without_stage_moves()`: a write that is Relay's own bookkeeping earns no stage.
    _stage_quiet = False

    @contextlib.contextmanager
    def without_stage_moves(self):
        """For an entry that is bookkeeping, not the start of a discussion.

        #3XZV moves an inbox card to Discussing on its thread's first entry.  An import's
        provenance note ("imported from …") is such an entry and nobody said anything: the card
        it lands belongs in the inbox, where `board_import_apply` says it put it.
        """
        previous, self._stage_quiet = self._stage_quiet, True
        try:
            yield
        finally:
            self._stage_quiet = previous

    def stage_advance(self, card_id: str, event: str) -> dict | None:
        """Move a work card one step along its stage lifecycle (#3XZV).

        This is the board itself acting — the same standing as `set_priority`, not a tool the
        model calls: it takes no `base_hash`, is not offered in `tool_specs`, and never refuses
        loudly. A card that is not in the event's starting statuses (further along, closed, or
        another type) is left exactly where it is and None comes back, so a caller can fire the
        event on every path that meets it. A manual `section:` is never touched: a card parked
        by hand stays parked while its stage moves underneath it.
        """
        if self._stage_quiet:
            return None
        step = STAGE_MOVES.get(event)
        if step is None:
            raise BoardToolError(f"unknown stage event {event!r}; use one of {', '.join(sorted(STAGE_MOVES))}.")
        froms, to, why = step
        card = self.board.card_by_id(normalize_id(card_id))
        if card is None or card.type != "work" or card.status not in froms or not card.path:
            return None
        if event == "discussed":
            # Only on the thread's first non-event entry; the event kinds a write itself makes
            # (an event) never count.
            if not any(e.kind != "event" for e in self.board.thread(card.id, card.private)):
                return None
        if event == "plan-written":
            if not any(_heading_matches(h, PLAN_HEADING) for h in section_headings(card.body)):
                return None
        before_bytes = card.path.read_bytes()
        base_hash = B.file_hash(card.path)
        old_status = card.status
        card.set("status", to)
        rank = self._rank_for(card, to, None, None)
        if rank is not None:
            card.set("rank", rank)
        category = self.board.category_of(card.path)
        target = B.card_target_path(self.board, card, category)
        moved_from = card.path if target is not None else None
        if target is not None and target.exists():
            raise BoardToolError(f"a different file already sits at {target.relative_to(self.board.repo)}.")
        size = self._thread_size(card)
        self.board.save(card, base_hash=base_hash)
        if target is not None:
            B.move_card_file(card, target)
        self.writes_this_turn += 1
        summary = f"{_column_label(old_status)} → {_column_label(to)} · {why}"
        self._append(card, f"- ✦ {self.context.actor} moved this card · {summary}", kind="event")
        write_id = self._record("move", card, summary, before_bytes, size, moved_from)
        return {"id": card.id, "status": to, "hash": B.file_hash(card.path), "write_id": write_id,
                "summary": summary}

    def _rank_for(self, card: B.Card, status: str, before, after) -> str | None:
        if before is None and after is None:
            return None if card.status == status and card.rank else self.board.next_rank(
                [c for c in self.board.cards() if c.status == status and c.id != card.id])
        column = sorted((c for c in self.board.cards()
                         if c.status == status and c.id != card.id and c.rank),
                        key=lambda c: c.rank)
        by_id = {c.id: c for c in column}

        def rank_of(value, what):
            key = normalize_id(value, what)
            neighbour = by_id.get(key)
            if neighbour is None:
                raise BoardToolError(f"#{key} is not a card in the {_column_label(status)} column.")
            return neighbour.rank

        low = high = None
        if after is not None:
            low = rank_of(after, "after")
            later = [c.rank for c in column if c.rank > low]
            high = later[0] if later else None
        if before is not None:
            high = rank_of(before, "before")
            earlier = [c.rank for c in column if c.rank < high]
            if after is None:
                low = earlier[-1] if earlier else None
        return B.rank_between(low, high)

    def _comment(self, args: dict) -> dict:
        if set(args) - {"id", "kind", "text", "pane_token"}:
            raise BoardToolError("board_comment takes id, kind, text and pane_token.")
        card_id = normalize_id(args.get("id"))
        kind = str(args.get("kind") or "").strip().lower()
        if kind not in COMMENT_KINDS:
            raise BoardToolError(f"kind must be one of {', '.join(COMMENT_KINDS)}.")
        text = _text(args.get("text"), "text", MAX_TEXT)
        card = self._card(card_id)
        if kind == "decision" and not _QUOTE_RE.search(text):
            raise BoardToolError(
                'A decision entry quotes the user\'s own words in quotation marks, e.g. '
                '2026-09-17, owner: "cloud is fine" → default to the cloud model. Quote them, '
                "then repeat the call.", code="board_refused", requires="verbatim_quote")
        # The pane a Run hand-off landed in (#HKAP): carried in the entry's attrs so the
        # GUI can draw the entry as a link that reveals that pane. Kept to what the entry
        # marker can hold — short, no whitespace, no '>' closing it early.
        pane_token = check_pane_token(args.get("pane_token")) or ""
        size = self._thread_size(card)
        before = card.path.read_bytes()
        entry = self._append(card, text, kind=kind, **({"pane_token": pane_token} if pane_token else {}))
        self.writes_this_turn += 1
        write_id = self._record("comment", card, f"{kind}: {text.splitlines()[0][:120]}", before, size)
        # The stage move the comment makes (#3XZV): the thread's first non-event entry moves an
        # inbox card to discussing. Relay's own move — inside, it is another write like this one.
        # A Refine turn's note is the exception (#6W9X): Refine checks the request before anything
        # is decided, so the card stays in whatever stage it was.
        if not (isinstance(self.card_scope, CardScope) and self.card_scope.mode == "refine"):
            self.stage_advance(card.id, "discussed")
        return {"id": card.id, "entry_id": entry.entry_id, "kind": kind, "write_id": write_id}

    def _claim(self, args: dict) -> dict:
        """`board_claim`: this terminal pane takes the card, in one call (protocol 19.19, #R9G7).

        The same four writes the Board's Run button makes, in the same order —
        `assignee: agent`, status `executing`, the card's `session` set to this pane's token, and
        a `progress` entry carrying that token so the thread draws it as a link back to this
        pane — plus the card itself in the result, so the turn that claimed it has the front
        matter, body, tasks and thread tail in context from here on instead of reading it again.

        A card another session already holds is the case this tool exists for: it is refused with
        `board_claimed_elsewhere`, naming that session and how old its last thread entry is, so
        two panes cannot start the same work without one of them seeing the other.
        """
        allowed = {"id", "note", "force"}
        if set(args) - allowed:
            raise BoardToolError(f"board_claim takes {', '.join(sorted(allowed))}.")
        card_id = normalize_id(args.get("id"))
        note = args.get("note")
        if note is not None and not isinstance(note, str):
            raise BoardToolError("note must be text: a line or two on what you are about to do.")
        note = _text(note, "note", MAX_TEXT) if (note or "").strip() else ""
        force = args.get("force")
        if force is not None and not isinstance(force, bool):
            raise BoardToolError("force must be true or false.")
        token = self.pane_token or ""
        card = self._card(card_id)
        verify_before = card.front.get("verify")       # the QA policy floor (#C3Q2), see below
        if card.type != "work":
            raise BoardToolError(f"#{card_id} is a {card.type} card; only a work card is claimed "
                                 "and worked. Use board_comment to say something about this one.")
        held = str(card.front.get("session") or "").strip()
        if held and held != token and card.status in CLAIMED_STATUSES and not bool(force):
            entries = self.board.thread(card_id, card.private)
            latest = max((e.entry_id for e in entries), default="")
            raise BoardToolError(
                f"#{card_id} is held by another session ({held[:8]}): it is "
                f"{_column_label(card.status)} with a `session` of its own, and the last entry on "
                f"its thread is {latest or 'none'}. That session's work is not yours to take: read "
                "the thread, say what you are doing instead in a `progress` comment, and ask the "
                "user before you repeat this call with force: true.",
                code="board_claimed_elsewhere", id=card_id, session=held[:8],
                status=card.status, latest_entry=latest)

        before_bytes = card.path.read_bytes()
        base_hash = B.file_hash(card.path)
        old_status = card.status
        parts: list[str] = []
        if str(card.front.get("assignee") or "").strip() != "agent":
            card.set("assignee", "agent")
            parts.append("assignee agent")
        status = old_status
        if old_status not in CLAIMED_STATUSES:
            status = "executing"
            card.set("status", status)
            rank = self._rank_for(card, status, None, None)
            if rank is not None:
                card.set("rank", rank)
            parts.append(f"{_column_label(old_status)} → {_column_label(status)}")
        # The same stamp `board_move_card` makes on the way into executing (#T71W): the pane
        # that claims the card is the one implementing it.
        mine = self.context.signature()
        if mine and str(card.front.get("implemented_by") or "") != mine:
            card.set("implemented_by", mine)
            parts.append(f"implemented_by {mine}")
        # A card claimed with no `verify` block inherits the loaded skill's (#MSJ0).
        block, defaulted_from, verify_note = self._verify_from_skills(card)
        if block is not None:
            card.set("verify", block)
            parts.append(verify_note)
        if token:
            if held != token:
                card.set("session", token)
                parts.append(f"session {token[:8]}")
        elif held:
            # Forced a card away from the session that held it, with no token to put in its
            # place: leaving the old one there would tell the next pane the card is still taken.
            card.drop("session")
            parts.append(f"session {held[:8]} released")

        category = self.board.category_of(card.path)
        target = B.card_target_path(self.board, card, category)
        moved_from = card.path if target is not None else None
        if target is not None and target.exists():
            raise BoardToolError(f"a different file already sits at {target.relative_to(self.board.repo)}.")
        # A `verify` block the claim put on the card meets the QA policy floor (#C3Q2).
        qa_notes = (self._apply_qa_floor(card) if card.front.get("verify") is not None
                    and card.front.get("verify") != verify_before else [])
        size = self._thread_size(card)
        self.board.save(card, base_hash=base_hash)
        if target is not None:
            B.move_card_file(card, target)
        self.writes_this_turn += 1
        summary = ", ".join(parts) or "already claimed by this pane"
        self._append(card, f"- ✦ {self.context.actor} claimed this card · {summary}", kind="event")
        line = CLAIM_LINE.format(short=token[:8]) if token else CLAIM_LINE_NO_TOKEN
        text = f"{line}\n\n{note}" if note else line
        entry = self._append(card, text, kind="progress",
                             **({"pane_token": token} if token else {}))
        write_id = self._record("claim", card, summary, before_bytes, size, moved_from)
        if card_id not in self.claimed:
            self.claimed.append(card_id)
        card = self.board.card_by_id(card_id) or card
        # One line, once, at the moment the work starts (#WFRA): a card claimed without a
        # `verify` block is asked for one beside `## Done means`, before any code.
        reminder = ("" if card.front.get("verify") is not None else
                    f"#{card_id} has no `verify` block: propose one with board_update_card "
                    "fields.verify (artifact, primary, also, human, criteria, sign_off, effort, …) "
                    "beside `## Done means` before you write code.")
        return {"claimed": True, "id": card_id, "status": status,
                "session": token or None, "entry_id": entry.entry_id,
                "hash": B.file_hash(card.path), "write_id": write_id, "summary": summary,
                **({"reminder": reminder} if reminder else {}),
                **({"qa_policy": qa_notes} if qa_notes else {}),
                **({"verify_defaulted_from": defaulted_from} if defaulted_from else {}),
                **({"note": verify_note} if verify_note else {}),
                **({} if token else {"warning": NO_TOKEN_NOTE}),
                "card": card_block(self.board, card)}

    # ---- the tests a card names (protocol 31, #7BM4) ---------------------------
    def _tests(self):
        """This project's `tests_protocol.TestsCommands`, made on first use.

        Imported inside the method, not at the top of the module: it pulls in `test_probe` and
        `jobs`, and most turns never ask about a test.  Its `emit` is the default no-op — a tool
        call answers the model with its return value, and the pane's own events come from the
        board worker's instance (`board_protocol._tests`), not from this one.
        """
        if self._tests_commands is None:
            from . import tests_protocol as TP
            # The pane token goes with it (#AQ6X §32.8): every run leaves a `run` line naming the
            # pane whose it was, and that is the only thing that can tell a signal *this* card's
            # work opened from one that was already failing. Without it the gate blocks nothing.
            self._tests_commands = TP.TestsCommands(self.board.repo, self.board.root,
                                                   pane_token=self.pane_token)
        return self._tests_commands

    def _tests_check(self, args: dict) -> dict:
        """`tests_check`: what moved under one card's `## Tests` section.  Runs nothing."""
        from . import tests_protocol as TP
        card_id = normalize_id(args.get("card") or args.get("id"))
        if not card_id:
            raise BoardToolError("tests_check needs `card`: the card id, e.g. K7Q2.")
        try:
            result = self._tests().check_card(card_id)
        except ValueError as exc:
            raise BoardToolError(str(exc), code="tests_refused") from exc
        return {"card": result["card"], "text": TP.format_findings(result),
                # The statuses are the answer (#PR4Q), so the agent decides what the card page
                # decides: `passed`, `failed`, `missing-evidence` or `not-applicable` per check.
                "statuses": result.get("statuses") or [], "findings": result["findings"],
                "actions": result["actions"]}

    def _tests_run(self, args: dict) -> dict:
        """`tests_run`: run the named tests and wait for the table.

        The refusals are `tests_protocol`'s own — an empty list, more than the ceiling, a second
        run while one is in flight, a missing build directory, ids that name only `manual:`
        evidence — carried through as tool errors so the model reads the sentence and corrects
        the call rather than seeing a traceback.
        """
        from . import tests_protocol as TP
        ids = args.get("ids")
        if not isinstance(ids, list) or not ids:
            raise BoardToolError("tests_run needs `ids`: the tests to run, each "
                                 "`<runner>:<invocation>`. There is no way to run the whole "
                                 "suite from here.")
        try:
            timeout = int(args.get("timeout_seconds") or DEFAULT_TEST_TIMEOUT)
        except (TypeError, ValueError):
            raise BoardToolError("tests_run timeout_seconds must be a number.") from None
        try:
            result = self._tests().run_and_wait(
                ids, timeout=max(10, min(MAX_TEST_TIMEOUT, timeout)),
                repeat_until_fail=args.get("repeat_until_fail") or 0,
                max_ids=MAX_AGENT_TEST_IDS)
        except TP.TestsError as exc:
            raise BoardToolError(str(exc), code="tests_refused") from exc
        return {"text": TP.format_run(result), **result}

    def _board_try(self, args: dict) -> dict:
        """`board_try`: the Try it brief for one card, for an agent that has a machine to run it on.

        It hands back the same prompt the Board's Try it button runs
        (`tryit_protocol.tryit_prompt`) rather than starting a turn somewhere else, because there
        is nowhere else to start one: a terminal pane's worker and the Board's worker are
        different processes and the board is the only thing between them. The calling agent
        already has a shell, a display and this turn — it *is* the machine — so the useful act is
        to give it the brief and the card, which is what `/deliver`'s landing step needs.

        The thread gains a `progress` entry, so a person watching the card sees that a Try it was
        prepared and by whom, exactly as a claim or a check does.
        """
        if set(args) - {"card"}:
            raise BoardToolError("board_try takes card.")
        from . import tryit_protocol as TI
        card_id = normalize_id(args.get("card"), "card")
        card = self._card(card_id)
        prompt = TI.tryit_prompt(self, card_id, TI.evidence_dir_for(self.board.repo, card_id))
        staged = TI.verify_staging(self.board.repo, card_id, card.body)
        self._append(card, f"- ✦ {self.context.actor} is preparing Try it for this card"
                           + (f" · reusing the staging in {staged['dir']}"
                              if staged and staged.get("stage") else ""),
                     kind="progress")
        return {"card": card_id, "text": prompt, "evidence_dir":
                str(TI.evidence_dir_for(self.board.repo, card_id).relative_to(self.board.repo)),
                "reusing": bool(staged and staged.get("stage")),
                "staged": (staged or {}).get("stage") or ""}

    # ---- the case ledger (#95VZ) ------------------------------------------------
    def own_workspace(self) -> bool:
        """Whether the pane these tools serve works *in* this board's project.  The Board
        worker and a test pass no workspace and count as the board's own; a pane pointed at
        another project's board (`set_board`) does not, and then the ledger's confidential
        rows are not its to read."""
        if self.workspace is None:
            return True
        try:
            here, repo = self.workspace.resolve(), Path(self.board.repo).resolve()
        except OSError:                                     # pragma: no cover - unreadable path
            return False
        return here == repo or here.is_relative_to(repo)

    def ledger(self, *, server: str | None = None, card: str | None = None,
               limit: int | None = None) -> list[dict]:
        """The ledger's rows, confidential ones included only for the board's own workspace."""
        return CASES.read(self.board.root, server=server, card=card, limit=limit,
                          include_confidential=self.own_workspace())

    def _profiled_skill(self) -> tuple[str | None, dict]:
        """The one skill this turn loaded that carries a profile, or (None, {}) when there is
        none or more than one — the same rule `_verify_from_skills` applies."""
        profiled = {sid: prof for sid, prof in self.context.skills.items()
                    if isinstance(prof, dict) and prof}
        if len(profiled) != 1:
            return None, {}
        (sid, prof), = profiled.items()
        return sid, prof

    def _server_for_card(self, card_id: str | None) -> tuple[str, str, dict]:
        """`(server, server_version, profile)` for a row about `card_id`: the loaded profiled
        skill this turn, else what the ledger's newest row about the card says, else the
        card itself (`card:<ID>`, a card that served as its own server)."""
        sid, profile = self._profiled_skill()
        if sid:
            return sid, self.context.skill_versions.get(sid, ""), profile
        if card_id:
            prior = CASES.read(self.board.root, card=card_id, limit=1)
            if prior:
                return (str(prior[-1].get("server") or f"card:{card_id}"),
                        str(prior[-1].get("server_version") or ""),
                        {"confidential": "yes"} if prior[-1].get("confidential") else {})
        return f"card:{card_id}" if card_id else CASES.PERSON, "", {}

    def _verified_cases(self, card: B.Card) -> int:
        """What `qa_policy`'s `ai_may_gate_after` / `sample_after` count against: passing
        rows for the server that serves this card, or across the board when no row and no
        loaded skill says which server that is."""
        try:
            rows = CASES.read(self.board.root)
        except OSError:                                     # pragma: no cover - unreadable ledger
            return 0
        if not rows:
            return 0
        server, _, _ = self._server_for_card(card.id)
        mine = CASES.verified_count(rows, server) if not server.startswith("card:") else 0
        return mine if mine else CASES.verified_count(rows)

    def record_case(self, *, card: str | None = None, verdict=None, served_by: str | None = None,
                    server: str | None = None, server_version: str | None = None, cost=None,
                    signal=None, escalated=None, input: str | None = None,
                    confidential: bool | None = None) -> dict | None:
        """Append one row for a case this pane served or judged; None when the board has no
        directory to write in or the row cannot be written.  Never raises on the write —
        a ledger that cannot be appended must not fail the card write it rides on — and never
        emits an event: the row is agent-facing (owner, 2026-09-23)."""
        who = served_by or self.context.signature() or self.context.actor
        srv, ver, profile = self._server_for_card(card)
        if server:
            srv = server
            ver = server_version or (CASES.PERSON if server == CASES.PERSON else "")
            profile = self.context.skills.get(server) or {}
        elif server_version:
            ver = server_version
        if confidential is None:
            confidential = str(profile.get("confidential") or "").lower() == "yes"
        try:
            row = CASES.new_record(srv, served_by=who, server_version=ver, card=card, input=input,
                                   cost=cost, signal=signal, escalated=escalated, verdict=verdict,
                                   confidential=confidential)
        except CASES.CaseError as exc:
            raise BoardToolError(str(exc), code="board_refused") from exc
        try:
            return CASES.append(self.board.root, row)
        except OSError:
            return None

    def record_turn_cases(self, *, outcome: str = "done", cost=None) -> list[dict]:
        """The end of a turn that loaded a profiled skill (#95VZ): one row per such skill,
        pending, costed with the turn's usage, about the last card the turn wrote to.  Called
        by `Agent._end_turn`; a turn that loaded no profiled skill writes nothing."""
        rows = []
        card = self.cards_this_turn[-1] if self.cards_this_turn else None
        turn = self.context.attrs().get("turn")
        for sid, profile in list(self.context.skills.items()):
            if not isinstance(profile, dict) or not profile:
                continue
            row = self.record_case(server=sid, server_version=self.context.skill_versions.get(sid, ""),
                                   card=card, cost=cost, verdict="pending",
                                   signal={"mode": profile.get("primary", ""), "result": outcome},
                                   input=f"turn {turn}" if turn else None)
            if row is not None:
                rows.append(row)
        return rows

    def _case(self, args: dict) -> dict:
        """`board_case`: the agent logs a case a person served (or one it served outside a
        card).  The result says how many rows the server now has and, at the third
        person-served case of one server inside 90 days, `third_case_hint: true` with the
        one line the agent may put in its reply (owner, 2026-09-23: a line, never a card)."""
        allowed = {"server", "served_by", "cost", "input", "card", "verdict", "signal", "escalated"}
        if set(args) - allowed:
            raise BoardToolError(f"board_case takes {', '.join(sorted(allowed))}.")
        server = args.get("server")
        if not isinstance(server, str) or not server.strip():
            raise BoardToolError("board_case needs server: a skill id, a program path, or 'person'.")
        server = server.strip()
        served_by = args.get("served_by")
        if served_by is not None and (not isinstance(served_by, str) or not served_by.strip()):
            raise BoardToolError("served_by must be 'person' or a model signature.")
        served_by = (served_by or CASES.PERSON).strip()
        card_id = normalize_id(args["card"], "card") if args.get("card") else None
        if card_id:
            self._card(card_id)
        skill = self.context.skills.get(server)
        version = self.context.skill_versions.get(server) if skill is not None else None
        if version is None:
            version = CASES.PERSON if server == CASES.PERSON else ""
        row = self.record_case(server=server, server_version=version, served_by=served_by,
                               card=card_id, cost=args.get("cost"), input=args.get("input"),
                               verdict=args.get("verdict"), signal=args.get("signal"),
                               escalated=args.get("escalated"))
        if row is None:
            raise BoardToolError("the case ledger could not be written.", code="board_error")
        rows = CASES.read(self.board.root, server=server)
        hint = CASES.third_case_hint(rows, server)
        return {"case": row["id"], "when": row["when"], "server": server,
                "served_by": row["served_by"], "cases": len(rows),
                "confidential": row["confidential"], "third_case_hint": hint,
                **({"hint": CASES.hint_line(server)} if hint else {})}

    def _list_cases(self, args: dict, limit: int) -> dict:
        """`board_list {cases: true}`: the last `limit` rows, oldest first, filtered by
        `server` / `card`; confidential rows only for the board's own workspace."""
        for key in ("tab", "status", "type", "labels", "query"):
            if args.get(key) is not None:
                raise BoardToolError(f"{key} filters cards, not cases; with cases: true pass "
                                     "server or card.")
        server = args.get("server")
        if server is not None and (not isinstance(server, str) or not server.strip()):
            raise BoardToolError("server must be a skill id, a program path, or 'person'.")
        card = normalize_id(args["card"], "card") if args.get("card") else None
        rows = self.ledger(server=server.strip() if server else None, card=card)
        return {"cases": rows[-limit:], "total": len(rows), "truncated": len(rows) > limit,
                "path": str(CASES.ledger_path(self.board.root).relative_to(self.board.repo)),
                "confidential_hidden": not self.own_workspace()}

    # ---- the faults the machine tracks (protocol 32, #AQ6X) --------------------
    def _signal_state(self):
        """`(signals module, {key: Signal})` for this board.  Reads two files, runs nothing.

        No discovery: `removed` is a verdict about what the project still *collects*, and
        collecting it means a `ctest --show-only` subprocess.  The fold that runs after a test
        run has that list already (`tests_protocol`); a tool call does not need it, and a signal
        listed here that has since left discovery is at worst one stale row.
        """
        from . import signals as S
        return S, S.state(self.board.repo, self.board.root)

    def _signals(self, args: dict) -> dict:
        """`board_signals`: list the open faults, or claim, release, dismiss or promote one.

        The refusals are `board_claim`'s, deliberately: a signal is claimed with the same pane
        token, a second claimant gets the same `board_claimed_elsewhere` naming the holder, and
        `force` is the same escape hatch with the same rule about asking first (R11).
        """
        allowed = {"action", "key", "reason", "comment", "until", "force"}
        if set(args) - allowed:
            raise BoardToolError(f"board_signals takes {', '.join(sorted(allowed))}.")
        action = str(args.get("action") or "").strip().lower()
        if action not in SIGNAL_ACTIONS:
            raise BoardToolError(f"board_signals action is one of {', '.join(SIGNAL_ACTIONS)}.")
        force = args.get("force")
        if force is not None and not isinstance(force, bool):
            raise BoardToolError("force must be true or false.")
        S, signals = self._signal_state()
        if action == "list":
            return self._signals_list(S, signals)
        if action != "list" and self.readonly:
            raise BoardToolError(
                "This turn writes nothing by design — the owner has not confirmed anything yet. "
                "Say what you would do; the write happens once the owner answers.",
                code="board_readonly_turn")
        if action != "list" and self.enforce_limits and self.autonomy == "off":
            raise BoardToolError("Board writes are turned off for this workspace "
                                 "(autonomy: off).", code="board_autonomy_off")
        key = str(args.get("key") or "").strip()
        if not key:
            raise BoardToolError(f"board_signals {action} needs `key`: the signal's key, as "
                                 "`list` gives it (e.g. ctest:panelayout).")
        refresh = key
        signal = signals.get(key)
        if signal is None or signal.state in ("resolved", "removed"):
            raise BoardToolError(
                f"There is no open signal {key!r}. Call board_signals with action list to see "
                "what is open — a signal resolves by its check passing, so one that is gone was "
                "fixed.", code="signal_not_found", key=key)
        path = S.default_path(self.board.repo, self.board.root)
        try:
            if action == "claim":
                return self._signal_claim(S, signal, path, bool(force))
            if action == "release":
                return self._signal_release(S, signal, path, args.get("reason"))
            if action == "dismiss":
                return self._signal_dismiss(S, signal, path, args)
            return self._signal_promote(S, signals, signal, path, "by hand")
        finally:
            self._refresh_signal_card(S, refresh)

    def _refresh_signal_card(self, S, key: str) -> None:
        """Put the promoted card's `## Signal` section back in step with the signal.

        A claim, a release and a dismissal all change what that section says, and the section is
        the machine's paragraph on a card a person reads — so it is refolded and rewritten here
        rather than waiting for the next test run.  A signal with no card, or a board that cannot
        be written, is nothing to do.
        """
        try:
            signal = S.state(self.board.repo, self.board.root).get(key)
            if signal is not None and signal.card:
                S.rewrite_section(self.board, signal)
        except Exception:                                  # pragma: no cover - defensive
            pass

    def _signals_list(self, S, signals: dict) -> dict:
        """What `list` answers with: the open rows in R11's order, plus the two counts."""
        payload = S.summary(signals.values(), session=self.pane_token or "")
        lines = []
        for row in payload["open"]:
            marks = "".join(m for m in (" regressed" if row.get("regressed") else "",
                                        " stale" if row.get("stale") else ""))
            held = f" · held by {row['session'][:8]}" if row.get("session") else ""
            card = f" · card #{row['card']}" if row.get("card") else ""
            lines.append(f"- {row['key']} · {row['kind']} · {row['count']} failure(s) · "
                         f"last {row['last_seen']}{marks}{held}{card}")
            if row.get("message"):
                lines.append(f"    {row['message'][:200]}")
        text = "\n".join(lines) or "No open signals: every check this project records is passing."
        return {**payload, "text": text,
                "promotable": [s.key for s in S.sort_signals(
                    [s for s in signals.values() if s.promote])]}

    def _signal_claim(self, S, signal, path: Path, force: bool) -> dict:
        token = self.pane_token or ""
        held = signal.session
        if held and held != token and not force:
            raise BoardToolError(
                f"{signal.key} is held by another session ({held[:8]}): it has failed "
                f"{signal.count} time(s) and that session has been on it since its last action. "
                "That session's work is not yours to take: say what you are doing instead, and "
                "ask the user before you repeat this call with force: true.",
                code="board_claimed_elsewhere", key=signal.key, session=held[:8],
                last_seen=signal.last_seen)
        S.append_event({"action": "claim", "key": signal.key, "session": token}, path)
        return {"claimed": True, "key": signal.key, "session": token or None,
                "kind": signal.kind, "count": signal.count, "excerpt": signal.excerpt,
                **({} if token else {"warning": NO_TOKEN_NOTE}),
                "text": (f"{signal.key} is yours: {signal.count} failing execution(s), "
                         f"{signal.kind}. It resolves on "
                         f"{S.RESOLVE_PASSES.get(signal.kind, 2)} consecutive passing "
                         "executions of that key and on nothing else, so run it to close it.")}

    def _signal_release(self, S, signal, path: Path, reason) -> dict:
        if reason is not None and not isinstance(reason, str):
            raise BoardToolError("reason must be text.")
        reason = _one_line(reason, "reason", MAX_REASON) if (reason or "").strip() else ""
        S.append_event({"action": "release", "key": signal.key, "reason": reason,
                        "session": self.pane_token or ""}, path)
        out = {"released": True, "key": signal.key, "reason": reason or None}
        if reason == S.GAVE_UP:
            # Promotion trigger (a) of R9: the agent that held it could not fix it, so it is a
            # person's problem now and the card is written in the same call.
            promoted = self._signal_promote(S, None, signal, path, S.GAVE_UP, released=True)
            out.update({k: v for k, v in promoted.items() if k != "released"})
            return out
        out["text"] = f"{signal.key} is free again."
        return out

    def _signal_dismiss(self, S, signal, path: Path, args: dict) -> dict:
        try:
            dismissal = S.check_dismissal(args.get("reason"), args.get("comment"),
                                          args.get("until"), by_agent=True)
        except S.SignalError as exc:
            raise BoardToolError(str(exc), code=exc.code, key=signal.key) from exc
        S.append_event({"action": "dismiss", "key": signal.key,
                        "by": self.pane_token or "", **dismissal}, path)
        return {"dismissed": True, "key": signal.key, **dismissal,
                "text": (f"{signal.key} is dismissed as {dismissal['reason']} until "
                         f"{dismissal['until']}. It keeps counting underneath and comes back "
                         "then; it blocks no card in the meantime.")}

    def _signal_promote(self, S, signals, signal, path: Path, reason: str,
                        released: bool = False) -> dict:
        """Write the bug card for a signal, through the ordinary card write path (R9).

        `status: inbox` in the bugs tab, the failure excerpt as the card's own words, the label
        `signal`, `links.signal` back to the key, and the machine-owned `## Signal` section.  The
        signal records the card id, and from then on the card cannot leave `needs-verification`
        while the signal is open (`_signal_gate`) — closing the card resolves nothing.
        """
        if signal.card:
            return {"promoted": False, "key": signal.key, "card": signal.card,
                    "text": f"{signal.key} is already card #{signal.card}."}
        if signals is None:
            _, signals = self._signal_state()
        promoted = [s for s in S.promoted_open(signals.values()) if s.key != signal.key]
        if len(promoted) >= S.MAX_PROMOTED_OPEN:
            raise BoardToolError(
                f"{S.MAX_PROMOTED_OPEN} promoted signal cards are already open "
                f"({', '.join('#' + s.card for s in promoted[:S.MAX_PROMOTED_OPEN])}): this one "
                "stays in the signal list until one of them is closed, so the backlog does not "
                "fill with machine-written cards.",
                code="signal_promote_cap", key=signal.key, limit=S.MAX_PROMOTED_OPEN)
        category = self._category_for_tab("bugs")
        cards = self.board.cards()
        taken = [c.id for c in cards if c.id]
        card = B.new_card("work", S.card_title(signal), "inbox", card_id=B.new_id(taken),
                          request=S.card_request(signal),
                          rank=self.board.next_rank([c for c in cards if c.status == "inbox"]),
                          labels=["bug", S.SIGNAL_LABEL],
                          source=f"signal {signal.key}, {datetime.now().strftime('%Y-%m-%d')}")
        links = dict(card.front.get("links") or {})
        links["signal"] = signal.key
        card.set("links", links)
        signal.card = card.front["id"]
        card.body = B.append_body_section(card.body, S.SIGNAL_HEADING,
                                          S.signal_section(signal))
        try:
            card_path = B.write_new_card(self.board, card, category)
        except B.BoardError as exc:
            raise BoardToolError(str(exc)) from exc
        self.creates_this_turn += 1
        self.writes_this_turn += 1
        rel = str(card_path.relative_to(self.board.repo))
        size = self._thread_size(card)
        self._append(card, f"- ✦ signal {signal.key} became this card · {reason} · {rel}",
                     kind="event")
        write_id = self._record("create", card, f"promoted {signal.key}", None, size)
        S.append_event({"action": "promote", "key": signal.key, "card": card.id,
                        "reason": reason, "session": self.pane_token or ""}, path)
        return {"promoted": True, "released": released, "key": signal.key, "card": card.id,
                "path": rel, "write_id": write_id,
                "text": (f"{signal.key} is now card #{card.id} in the bugs tab ({reason}). The "
                         "signal stays open until the check passes: closing the card does not "
                         "close it, and the card cannot leave needs-verification while it is "
                         "open.")}

    def promote_signal(self, key: str, reason: str = "") -> dict:
        """File the bug card for one signal, for the fold that noticed it was due (R9).

        Not a tool: the caller is `tests_protocol.fold_signals`, which has just recomputed the
        state and found a signal the rules promote — the agent's own route is
        `board_signals {action: promote}`.  A signal that is not open, or already has a card, is
        a no-op rather than an error, because two folds may notice the same one; the cap is a
        refusal and is returned as one, so the signal stays in the list.
        """
        S, signals = self._signal_state()
        signal = signals.get(str(key or ""))
        if signal is None or signal.state != "open" or signal.card:
            return {"promoted": False, "key": str(key or "")}
        path = S.default_path(self.board.repo, self.board.root)
        try:
            return self._signal_promote(S, signals, signal, path,
                                        reason or signal.promote or "due")
        except BoardToolError as exc:
            return exc.to_result()

    def _human_qa_gate(self, card: B.Card, status: str) -> None:
        """No agent closes a card over an unanswered `## Human QA` question (#WC3E).

        Owner, 2026-09-21: "a card with an open judgement waits for the person".  Agents move
        cards within their own authority -- the implementer to `needs-verification`, the verifier
        on to a QA lane or back a stage -- and this is the one move that is not theirs to make.
        The person at the keyboard still closes it: the refusal is on the *agent* actor, so the
        Board's own Done column is unaffected, which is what "waits for the person" means.

        A question is a numbered line in `## Human QA`; it is answered by an indented line under
        it beginning `Answer:` (docs/BOARD-FORMAT.md 2.7).  Prose with no numbered question
        gates nothing -- the section is a brief as often as it is a question list.
        """
        if status != "done" or self.context.actor == OWNER_ACTOR:
            return
        open_questions = unanswered_human_qa(card.body)
        if not open_questions:
            return
        shown = "; ".join(open_questions[:2]) + ("…" if len(open_questions) > 2 else "")
        # The reason is on the card when its `verify` block asks for a person (#1AA6).
        try:
            human = str((B.verify_block(card) or {}).get("human") or "")
        except B.BoardError:
            human = ""
        because = f" (verify.human: {human})" if human in ("optional", "required") else ""
        raise BoardToolError(
            f"#{card.id} has {len(open_questions)} unanswered question(s) in `## Human QA` "
            f"({shown}){because} — that judgement is the person's, so an agent does not move the "
            "card to done: leave it in its QA lane and ask.",
            code="board_refused", requires="human_qa_answer", id=card.id or "",
            offer="needs-qa-human", questions=open_questions)

    def _verify_gate(self, card: B.Card, status: str) -> None:
        """The `verify` block's three refusals (#1AA6), each with the move that is open instead.

        - `deferred` set: neither `done` nor a `needs-qa-*` lane, for anyone -- verification
          has not happened and the card says until when; clearing `deferred` through
          `fields.verify` is the only way on, and the update's thread event records who did.
        - `human: required` and no `## Human QA` question carries an `Answer:` line: `done` is
          refused for an agent (the person at the keyboard is the answer, as in
          `_human_qa_gate`) and the move offered is `needs-qa-human`.
        - `sign_off` other than `none` and no line beginning `Receipt:` in `## Verdict` or
          `## Execution Summary`: `done` is refused, naming the sign-off, for anyone -- a receipt
          is a recorded fact, not the closer's say-so.
        """
        if status != "done" and status not in QA_STATUSES:
            return
        try:
            verify = B.verify_block(card)
        except B.BoardError as exc:
            if status == "done":
                raise BoardToolError(
                    f"#{card.id} cannot be verified through a `verify` block nobody can read "
                    f"({exc}): fix it with board_update_card fields.verify first.",
                    code="board_refused", requires="verify", id=card.id or "") from exc
            return
        if not verify:
            return
        if verify.get("deferred"):
            until = B.deferred_text(verify)
            raise BoardToolError(
                f"#{card.id} is unverified until {until} (verify.deferred), so it does not move "
                f"to {status}; clear `deferred` through board_update_card fields.verify once it "
                "has been verified, and move it then.",
                code="board_refused", requires="verify_deferred", id=card.id or "", until=until)
        if status != "done":
            return
        if verify.get("human") == "required" and self.context.actor != OWNER_ACTOR:
            questions = B.human_qa_questions(card.body)
            if not any(answered for _, answered in questions):
                open_q = [q for q, answered in questions if not answered]
                missing = (f"the question {open_q[0]!r} has no `Answer:` line under it" if open_q
                           else "`## Human QA` holds no question for them yet — write one from "
                                f"the criteria: {verify.get('criteria', '')}")
                raise BoardToolError(
                    f"#{card.id} needs the person's answer before it is done (verify.human: "
                    f"required, and {missing}), so move it to needs-qa-human and the person "
                    "answers on the card.",
                    code="board_refused", requires="human_qa_answer", offer="needs-qa-human",
                    id=card.id or "", questions=open_q)
        sign_off = str(verify.get("sign_off") or "none")
        if sign_off != "none" and not B.has_receipt(card.body):
            raise BoardToolError(
                f"#{card.id} needs a {sign_off} sign-off before it is done: add a `Receipt:` "
                "line to `## Verdict` or `## Execution Summary` saying who confirmed it, when "
                "and where, then move it.",
                code="board_refused", requires="receipt", sign_off=sign_off, id=card.id or "")

    def _signal_gate(self, card: B.Card, old_status: str, status: str) -> None:
        """Decision 6 and 8: refuse a move out of `needs-verification` under an open signal.

        Only a signal this card is answerable for: the one it was promoted from, and the ones
        **first seen in a run by the pane holding this card** (`signals.blocking`).  A signal that
        was already open before this pane started is listed as `open_before` and refuses nothing —
        a session answers for what its own work broke, not for the state of the tree it found —
        and a dismissed signal never blocks, which is the owner's override.

        A board with no signal store, or a fold that cannot be computed, gates nothing: this is a
        rule about a fault that is *known*, and an unreadable private file is not a fault.
        """
        if old_status != SIGNAL_GATE_FROM or status == old_status:
            return
        try:
            S, signals = self._signal_state()
            blocks, before = S.blocking(signals.values(), card=card.id or "",
                                        session=str(card.front.get("session") or ""))
        except Exception:                                  # pragma: no cover - unreadable store
            return
        if not blocks:
            return
        shown = ", ".join(s.key for s in blocks[:3]) + ("…" if len(blocks) > 3 else "")
        raise BoardToolError(
            f"#{card.id} cannot leave {_column_label(old_status)} while "
            f"{len(blocks)} signal(s) this pane's own runs opened are still failing ({shown}): a "
            "signal resolves by its check passing, so run them and fix them, or dismiss one you "
            "have shown is not the code's fault (board_signals). "
            + (f"{len(before)} other signal(s) were open before this card and do not block it."
               if before else ""),
            code="board_signal_open", id=card.id or "",
            signals=[s.to_dict() for s in blocks],
            open_before=[s.to_dict() for s in before])

    # ---- releasing a claim (#R9G7, owner 2026-09-20: "auto-release on done and on closed") ----
    def _release_on_close(self, card: B.Card, status: str) -> str:
        """Drop the card's `session` when the write about to happen leaves it closed.

        A `session` means "this pane is working on it" (19.19), and a `done` or `dropped` card is
        nobody's: the work is over, so a claim left behind can only tell the next session a card
        is taken when it is not. A pane token is a fresh uuid per pane that no resume restores, so
        it cannot usefully outlive the card's own lifetime either.

        Called on the in-memory card *before* it is written, so the drop rides the same write and
        the same undo record as the status change; the caller names the release in its own thread
        entry rather than appending a second one. Returns the token released, or "".
        """
        if status not in B.CLOSED_ITEM_STATUSES:
            return ""
        held = str(card.front.get("session") or "").strip()
        if not held:
            return ""
        card.drop("session")
        if card.id in self.claimed:
            self.claimed = [c for c in self.claimed if c != card.id]
        return held

    def release_claims(self, reason: str = "the pane closed") -> list[str]:
        """Drop every claim this pane holds: the pane that held them has gone (19.19).

        Called from the worker's `shutdown` — a pane's destructor sends `cancel` then `shutdown`
        and waits 1.5 s — and from `BoardCommands._repoint` when this worker is pointed at another
        board, where the tools holding these claims are about to be thrown away.

        Only `session` goes. Status and `assignee` are left exactly as they are: the work is still
        in flight and the card still belongs in Executing, it is just not held by a live pane any
        more, so the next session finds it where it was and may claim it without `force`.

        It runs inside a closing pane's grace period, so it is deliberately cheap: one pass over
        the card files, a write only for the cards that name this token, no model, no network and
        no undo record (there is no window left to undo in). It never raises — a card it cannot
        read or write is skipped and the others are still released — and it does nothing at all
        for a worker with no pane token: the Board's own, or a test's.

        Nothing is announced on the pipe: the Board pane is drawn by a different worker and
        already learns of another pane's writes from its `QFileSystemWatcher` on the board folder
        (`BoardView::watchIssues`), which debounces 400 ms and then asks its own worker for a
        `board_refresh` — a fresh read of every card file, so the dropped field is simply there.

        Returns the ids it released.
        """
        token = (self.pane_token or "").strip()
        released: list[str] = []
        if not token:
            return released
        try:
            paths = self.board.card_paths()
        except OSError:
            return released
        for path in paths:
            try:
                card = B.Card.load(path)
                if (card.id is None or card.type != "work"
                        or card.status not in CLAIMED_STATUSES
                        or str(card.front.get("session") or "").strip() != token):
                    continue
                card.drop("session")
                self.board.save(card)
                self._append(card, RELEASE_LINE.format(short=token[:8], reason=reason),
                             kind="event")
                released.append(card.id)
            except Exception as exc:      # a card this cannot read is not worth a failed shutdown
                self._log_release_failure(path, exc)
        if released:
            self.claimed = [c for c in self.claimed if c not in released]
        self.release_signal_claims(reason)
        return released

    def release_signal_claims(self, reason: str = "the pane closed") -> list[str]:
        """Drop every *signal* this pane holds, for the same reason cards are dropped (#AQ6X).

        A signal's claim is the same pane token as a card's (R11), so the pane going takes both
        with it: otherwise the next session reads a fault as somebody else's and nobody runs it.
        One append per signal to the private event log — no card is touched, nothing is announced
        on the pipe, and a store it cannot read is skipped rather than failing a shutdown.
        """
        token = (self.pane_token or "").strip()
        if not token:
            return []
        try:
            from . import signals as S
            signals = S.state(self.board.repo, self.board.root)
            path = S.default_path(self.board.repo, self.board.root)
            released = [key for key, signal in signals.items() if signal.session == token]
            for key in released:
                S.append_event({"action": "release", "key": key, "session": token,
                                "reason": reason}, path)
        except Exception as exc:          # a store this cannot read is not worth a failed shutdown
            self._log_release_failure(Path(str(self.board.root)), exc)
            return []
        return released

    def _log_release_failure(self, path: Path, exc: BaseException) -> None:
        """One line on stderr for a card `release_claims` could not release.  Never raises."""
        try:
            print(f"board: release_claims skipped {path.name}: "
                  f"{type(exc).__name__}: {exc}", file=sys.stderr, flush=True)
        except Exception:
            pass

    # ---- cleanup-only writes ----------------------------------------------------
    def _merge(self, args: dict) -> dict:
        """`board_merge_cards`: fold redundant cards into one, losing nothing (protocol 19.9)."""
        allowed = {"into", "cards", "reason"}
        if set(args) - allowed:
            raise BoardToolError(f"board_merge_cards takes {', '.join(sorted(allowed))}.")
        into_id = normalize_id(args.get("into"), "into")
        reason = _one_line(args.get("reason"), "reason", MAX_REASON)
        source_ids = [normalize_id(c, "cards") for c in _string_list(args.get("cards"), "cards")]
        if not source_ids:
            raise BoardToolError("cards must name at least one card to merge in.")
        if len(source_ids) > 10:
            raise BoardToolError("merge at most 10 cards into one at a time.")
        if into_id in source_ids:
            raise BoardToolError(f"#{into_id} cannot be merged into itself.")
        if len(set(source_ids)) != len(source_ids):
            raise BoardToolError("cards lists the same card twice.")
        into = self._card(into_id)
        if into.status in ("done", "dropped"):
            raise BoardToolError(f"#{into_id} is closed ({into.status}); merge into an open card.")
        sources = [self._card(cid) for cid in source_ids]
        for src in sources:
            if src.type != into.type:
                raise BoardToolError(f"#{src.id} is a {src.type} card and #{into_id} is a "
                                     f"{into.type} card; merge like with like.")

        size = self._thread_size(into)
        # `B.merge_cards` writes every source as `dropped` itself, so the release rides that write
        # rather than `_move`'s (#R9G7): a merged-away card is closed and nobody's.
        released = {src.id: self._release_on_close(src, "dropped") for src in sources}
        result = B.merge_cards(self.board, into, sources, reason=reason,
                               category_of=lambda card: self.board.category_of(card.path))
        self.writes_this_turn += 1
        titles = ", ".join(f"#{m['id']} {m['title']}" for m in result["merged"])[:300]
        summary = f"merged {len(sources)} card(s) in: {titles}"
        self._append(into, f"- ✦ {self.context.actor} merged "
                           f"{', '.join('#' + m['id'] for m in result['merged'])} into this card · {reason}",
                     kind="event")
        for merged, src in zip(result["merged"], sources):
            self._append(src, f"- ✦ {self.context.actor} merged this card into #{into_id} · {reason} · "
                              "its text is kept here and copied there; this card stays as the record"
                              + (f" · session {released[src.id][:8]} released"
                                 if released.get(src.id) else ""),
                         kind="event")
        write_id = self._record("merge", into, summary, result["into_before"], size,
                                others=result["others"],
                                cards=[m["id"] for m in result["merged"]])
        return {"id": into.id, "path": result["into_path"], "hash": result["into_hash"],
                "merged": [{k: v for k, v in m.items() if k != "src"} for m in result["merged"]],
                "write_id": write_id, "summary": summary}

    def _split(self, args: dict) -> dict:
        """`board_split_card`: one card per piece of unrelated work (protocol 19.9)."""
        allowed = {"id", "parts", "reason", "close"}
        if set(args) - allowed:
            raise BoardToolError(f"board_split_card takes {', '.join(sorted(allowed))}.")
        card_id = normalize_id(args.get("id"))
        reason = _one_line(args.get("reason"), "reason", MAX_REASON)
        parts = args.get("parts")
        if not isinstance(parts, list) or not 2 <= len(parts) <= 10:
            raise BoardToolError("parts must be a list of 2 to 10 pieces.")
        card = self._card(card_id)
        if card.status in ("done", "dropped"):
            raise BoardToolError(f"#{card_id} is closed ({card.status}); there is nothing to split.")
        clean: list[dict] = []
        for index, part in enumerate(parts, 1):
            if not isinstance(part, dict) or set(part) - {"title", "request", "status", "tab", "labels"}:
                raise BoardToolError(f"part {index} takes title, request, status, tab and labels.")
            clean.append({"title": _one_line(part.get("title"), f"part {index} title", MAX_TITLE),
                          "request": _text(part.get("request"), f"part {index} request", MAX_REQUEST),
                          "status": str(part.get("status") or card.status).strip().lower(),
                          "tab": part.get("tab"),
                          "labels": _string_list(part.get("labels"), f"part {index} labels")})
        if self.enforce_limits:
            for _ in clean:
                if not self.rate.claim(self.limit("max_creates_per_hour")):
                    raise BoardToolError(
                        f"Board limit: {self.limit('max_creates_per_hour')} new cards per hour "
                        "for this workspace. Summarize the rest of the split in your reply.",
                        code="board_rate_limited", scope="hour")
        category = (B.MEMORY_FOLDER if card.type == "memory" else B.ALIAS_FOLDER if card.type == "alias"
                    else self.board.category_of(card.path))
        size = self._thread_size(card)
        # `close` is `B.split_card` writing this card as `dropped` itself, the other path that does
        # not go through `_move` (#R9G7): every piece moved out, so the claim goes with it.
        released = self._release_on_close(card, "dropped") if args.get("close") else ""
        result = B.split_card(self.board, card, clean, reason=reason, category=category,
                              close=bool(args.get("close")), tab_category=self._category_for_tab)
        self.creates_this_turn += len(clean)
        self.writes_this_turn += 1
        summary = ("split into " + ", ".join(f"#{c['id']} {c['title']}" for c in result["children"]))[:400]
        self._append(card, f"- ✦ {self.context.actor} {summary} · {reason}"
                           + (" · this card is closed; every piece moved out" if result["closed"] else "")
                           + (f" · session {released[:8]} released" if released else ""),
                     kind="event")
        for child in result["children"]:
            self.board.append_thread(child["id"], f"- ✦ {self.context.actor} split this card out of "
                                                  f"#{card_id} · {reason}",
                                     author=self.context.actor, kind="event", private=card.private,
                                     **self.context.attrs())
        write_id = self._record("split", card, summary, result["before"], size,
                                moved_from=result["moved_from"], others=result["others"],
                                cards=[c["id"] for c in result["children"]])
        return {"id": card.id, "path": result["path"], "hash": result["hash"],
                "children": result["children"], "closed": result["closed"],
                "write_id": write_id, "summary": summary}

    def _sections(self, args: dict) -> dict:
        """`board_sections`: the board's own columns and category folders (protocol 19.9).

        The four things a person can do to the section list — add, remove, merge, rename — are
        all this one call, because they are all one rewrite of `board.yaml`: `columns` is the
        ordered list (add and remove), `column_statuses` says what each one collects (merge), and
        `column_titles` is the name over an id that does not change (rename).  **No card moves
        and no status changes**, whichever of them you do: a section is a view of the statuses,
        so a card that was in a dropped section comes back in a section of its own rather than
        disappearing (`B.column_statuses_of`, and `Model::sections()` on the GUI side).
        """
        allowed = {"columns", "column_statuses", "column_titles", "tabs", "reason"}
        if set(args) - allowed:
            raise BoardToolError(f"board_sections takes {', '.join(sorted(allowed))}.")
        reason = _one_line(args.get("reason"), "reason", MAX_REASON)
        if all(args.get(k) is None for k in ("columns", "column_statuses", "column_titles", "tabs")):
            raise BoardToolError("board_sections takes columns, column_statuses, column_titles, "
                                 "tabs, or any of them together.")
        config = self.board.config()
        before_bytes = (self.board.config_path.read_bytes()
                        if self.board.config_path.exists() else b"")
        changes: list[str] = []

        # The statuses first: `columns` is checked against them, so a section invented in this
        # same call is a known section by the time the list is read.
        statuses = config.get("column_statuses") if isinstance(config.get("column_statuses"), dict) else {}
        statuses = {str(k): [str(s) for s in v] for k, v in statuses.items() if isinstance(v, list)}
        if args.get("column_statuses") is not None:
            statuses = _column_statuses(args.get("column_statuses"))

        if args.get("columns") is not None:
            columns = _string_list(args.get("columns"), "columns")
            if not columns:
                raise BoardToolError("columns must name at least one section.")
            # An id outside the known list is allowed only when this board says what it collects:
            # that is what makes an invented section possible without making a typo silent. An
            # explicit empty entry is a manual section — one that collects nothing on purpose
            # (#3XZV) — and counts as said just the same.
            unknown = [c for c in columns if c not in B.COLUMN_IDS and c not in statuses]
            if unknown:
                raise BoardToolError(
                    f"unknown section(s) {', '.join(unknown)}: either name one of "
                    f"{', '.join(B.COLUMN_IDS)}, or give it statuses of its own in column_statuses.")
            bad = [c for c in columns if not _SECTION_ID_RE.fullmatch(c)]
            if bad:
                raise BoardToolError(f"section id {bad[0]!r} must be lower-case letters, digits, - and _.")
            if len(set(columns)) != len(columns):
                raise BoardToolError("columns lists the same section twice.")
            if list(config.get("columns") or []) != columns:
                changes.append(f"columns: {', '.join(str(c) for c in config.get('columns') or [])} "
                               f"→ {', '.join(columns)}")
                config["columns"] = columns

        if args.get("column_statuses") is not None:
            listed = [str(c) for c in (config.get("columns") or [])]
            stray = sorted(set(statuses) - set(listed))
            if stray:
                raise BoardToolError(f"column_statuses names {', '.join(stray)}, which is not a "
                                     "section in columns. Add it to columns in the same call.")
            # One status, one section. Two sections collecting it would draw the same card twice,
            # and a drop on either would be a move to whichever the list happened to read first.
            seen: dict[str, str] = {}
            for column in listed:
                for status in B.column_statuses_of({**config, "column_statuses": statuses}, column):
                    if status in seen:
                        raise BoardToolError(f"both {seen[status]} and {column} collect "
                                             f"{status!r}; a status belongs to one section.")
                    seen[status] = column
            if statuses != (config.get("column_statuses") or {}):
                changes.append("column_statuses: " + ("; ".join(
                    f"{c} = {', '.join(statuses[c])}" for c in sorted(statuses)) or "cleared"))
                if statuses:
                    config["column_statuses"] = statuses
                else:
                    config.pop("column_statuses", None)

        if args.get("column_titles") is not None:
            titles = _column_titles(args.get("column_titles"))
            if titles != (config.get("column_titles") or {}):
                changes.append("column_titles: " + ("; ".join(
                    f"{c} → {titles[c]}" for c in sorted(titles)) or "cleared"))
                if titles:
                    config["column_titles"] = titles
                else:
                    config.pop("column_titles", None)

        if args.get("tabs") is not None:
            tabs = args.get("tabs")
            if not isinstance(tabs, list) or not 1 <= len(tabs) <= 20:
                raise BoardToolError("tabs must be a list of 1 to 20 entries.")
            clean: list[dict] = []
            seen: set[str] = set()
            for index, tab in enumerate(tabs, 1):
                if not isinstance(tab, dict) or set(tab) - {"id", "folder", "filter"}:
                    raise BoardToolError(f"tab {index} takes id and either folder or filter.")
                tab_id = _one_line(tab.get("id"), f"tab {index} id", 40).lower()
                if not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", tab_id):
                    raise BoardToolError(f"tab id {tab_id!r} must be lower-case letters, digits, - and _.")
                if tab_id in seen:
                    raise BoardToolError(f"tab {tab_id!r} is listed twice.")
                seen.add(tab_id)
                entry: dict = {"id": tab_id}
                if tab.get("folder"):
                    folder = _one_line(tab["folder"], f"tab {index} folder", 60)
                    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", folder):
                        raise BoardToolError(f"tab folder {folder!r} must be one plain directory name.")
                    entry["folder"] = folder
                elif tab.get("filter"):
                    entry["filter"] = _one_line(tab["filter"], f"tab {index} filter", 120)
                else:
                    raise BoardToolError(f"tab {tab_id!r} needs a folder or a filter.")
                clean.append(entry)
            folders = {t["folder"] for t in clean if t.get("folder")}
            orphaned = sorted({self.board.category_of(c.path) for c in self.board.cards()
                               if c.type == "work" and c.path} - folders)
            if orphaned:
                raise BoardToolError(
                    f"these folders still hold cards and no tab names them: {', '.join(orphaned)}. "
                    "Move those cards with board_move_card first, then drop the tab.",
                    code="board_refused", requires="empty_folder")
            if config.get("tabs") != clean:
                changes.append(f"tabs: {', '.join(str(t.get('id')) for t in config.get('tabs') or [])} "
                               f"→ {', '.join(t['id'] for t in clean)}")
                config["tabs"] = clean

        if not changes:
            raise BoardToolError("nothing to change: the board already has these sections.")
        B.write_config(self.board, config)
        self.writes_this_turn += 1
        summary = "; ".join(changes)[:400] + f" · {reason}"
        write_id = self._record_path("sections", self.board.config_path, "", summary,
                                     before_bytes or None, None, 0)
        return {"path": str(self.board.config_path.relative_to(self.board.repo)),
                "columns": list(config.get("columns") or []),
                "tabs": list(config.get("tabs") or []),
                "changes": changes, "write_id": write_id, "summary": summary}

    # ---- undo ------------------------------------------------------------------
    def undo(self, write_id: str) -> dict:
        record = self.writes.get(write_id)
        if record is None:
            raise BoardToolError(f"no undoable Board write {write_id!r}.", code="board_not_found")
        if record.undone:
            raise BoardToolError("that write was already undone.")
        current = record.path
        if record.moved_from is not None and current.exists():
            record.moved_from.parent.mkdir(parents=True, exist_ok=True)
            os.replace(current, record.moved_from)
            current = record.moved_from
        removed = []
        if record.before is None:
            if _tracked_by_git(self.board.repo, current):
                raise BoardToolError("that card is already committed; close it with board_move_card "
                                     "(done or dropped) instead of undoing the creation.")
            if current.exists():
                current.unlink()
            removed.append(record.card_id)
            if record.thread_path is not None and record.thread_path.exists():
                record.thread_path.unlink()
        else:
            B._atomic_write(current, record.before.decode("utf-8"))
            self._truncate_thread(record)
        # A merge or a split touched more than the card it was recorded against: put the
        # cards it folded in back where they were, and remove the cards it created.
        for path, before, moved_from in record.others:
            here = path
            if moved_from is not None and here.exists():
                moved_from.parent.mkdir(parents=True, exist_ok=True)
                os.replace(here, moved_from)
                here = moved_from
            if before is None:
                if _tracked_by_git(self.board.repo, here):
                    continue
                if here.exists():
                    here.unlink()
            else:
                B._atomic_write(here, before.decode("utf-8"))
        record.undone = True
        self._emit_board({"event": "board_changed", "upserts": [] if removed else [record.card_id],
                          "removed": removed, "write_id": write_id, "undo_of": write_id})
        return {"undone": write_id, "id": record.card_id, "action": record.action,
                "removed": bool(removed), "also_restored": len(record.others)}

    def _truncate_thread(self, record: WriteRecord) -> None:
        """Drop the entries this write appended, then record the undo itself."""
        path = record.thread_path
        if path is None or not path.exists():
            return
        with open(path, "r+b") as handle:
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX)
            try:
                if handle.seek(0, os.SEEK_END) > record.thread_size:
                    handle.truncate(record.thread_size)
            finally:
                fcntl.flock(handle.fileno(), fcntl.LOCK_UN)
        card = self.board.card_by_id(record.card_id)
        if card is not None:
            self._append(card, f"- ↩ {self.context.actor} undid: {record.summary}", kind="event")


# ------------------------------------------------------------------ text plumbing

def _primary_mode(card: B.Card) -> str:
    """The card's `verify.primary`, or "" when it has no readable block — the `signal.mode`
    of a ledger row about it (#95VZ)."""
    try:
        verify = B.verify_block(card)
    except B.BoardError:
        return ""
    return str((verify or {}).get("primary") or "")


def _status_rank(status: str) -> int:
    """Where a status sits on the board's stage order (`board._STATUS_ORDER`); unknown last."""
    try:
        return B._STATUS_ORDER.index(status)
    except ValueError:
        return len(B._STATUS_ORDER)


def preview_line(name: str, args: dict) -> str:
    """One line describing a write a tool call would make (the dry-run changelog)."""
    head = name.replace("board_", "").replace("_", " ")
    bits = [f"{key}: {_short(args[key], 160)}" for key in
            ("into", "cards", "title", "status", "tab", "kind", "columns", "tabs", "reason")
            if args.get(key)]
    if isinstance(args.get("parts"), list):
        bits.append("parts: " + ", ".join(_short(p.get("title"), 60) for p in args["parts"]
                                          if isinstance(p, dict)))
    if isinstance(args.get("fields"), dict):
        bits.append("fields: " + ", ".join(sorted(args["fields"])))
    return f"{head} — " + ("; ".join(bits) or "(no arguments)")


def _short(value, limit: int = 80) -> str:
    text = "(unset)" if value is None else (value if isinstance(value, str) else json.dumps(value, ensure_ascii=False))
    text = " ".join(str(text).split())
    return text[:limit] + ("…" if len(text) > limit else "")


def _one_line(value, what: str, maximum: int) -> str:
    if not isinstance(value, str) or not value.strip():
        raise BoardToolError(f"{what} must be a non-empty string.")
    text = " ".join(value.split())
    if len(text) > maximum:
        raise BoardToolError(f"{what} must be at most {maximum} characters.")
    return text


def _text(value, what: str, maximum: int) -> str:
    if not isinstance(value, str) or not value.strip():
        raise BoardToolError(f"{what} must be a non-empty string.")
    if len(value) > maximum:
        raise BoardToolError(f"{what} must be at most {maximum} characters.")
    return value.replace("\r\n", "\n").rstrip()


#: A section id: the same shape as a tab id, because both name a thing in `board.yaml`.
_SECTION_ID_RE = re.compile(r"[a-z0-9][a-z0-9_-]*")
MAX_SECTIONS = 20
MAX_SECTION_TITLE = 40


def _column_statuses(value) -> dict[str, list[str]]:
    """`{section: [status, ...]}`, checked: known statuses, nothing invented.

    Merging two sections is writing one of them with both sets of statuses and dropping the
    other from `columns`, so this is the one place that decides what a section may collect.
    An explicit empty list is a section that collects nothing — one a person fills by hand,
    parking cards in it with `board_move_card {section}` (#3XZV) — and it is kept as written.
    """
    if not isinstance(value, dict):
        raise BoardToolError("column_statuses must be an object of section -> statuses.")
    if len(value) > MAX_SECTIONS:
        raise BoardToolError(f"column_statuses takes at most {MAX_SECTIONS} sections.")
    out: dict[str, list[str]] = {}
    for key, listed in value.items():
        column = _one_line(key, "a column_statuses key", 40).lower()
        if not _SECTION_ID_RE.fullmatch(column):
            raise BoardToolError(f"section id {column!r} must be lower-case letters, digits, - and _.")
        if not isinstance(listed, list) or not all(isinstance(s, str) for s in listed):
            raise BoardToolError(f"column_statuses[{column}] must be an array of statuses.")
        statuses = [s.strip() for s in listed if s.strip()]
        if not listed:
            out[column] = []          # a manual section: it collects nothing on purpose
            continue
        unknown = [s for s in statuses if s not in B.ALL_STATUSES]
        if unknown:
            raise BoardToolError(f"unknown status(es) {', '.join(unknown)} in section {column!r}; "
                                 f"a section collects from: {', '.join(B.ALL_STATUSES)}.")
        if len(set(statuses)) != len(statuses):
            raise BoardToolError(f"section {column!r} lists the same status twice.")
        out[column] = statuses
    return out


def _column_titles(value) -> dict[str, str]:
    """`{section: "Name"}`: what the sections are called, over ids that do not change."""
    if not isinstance(value, dict):
        raise BoardToolError("column_titles must be an object of section -> name.")
    if len(value) > MAX_SECTIONS:
        raise BoardToolError(f"column_titles takes at most {MAX_SECTIONS} sections.")
    out: dict[str, str] = {}
    for key, title in value.items():
        column = _one_line(key, "a column_titles key", 40).lower()
        if not _SECTION_ID_RE.fullmatch(column):
            raise BoardToolError(f"section id {column!r} must be lower-case letters, digits, - and _.")
        if isinstance(title, str) and not title.strip():
            continue          # cleared: the section goes back to the name Relay gives it
        out[column] = _one_line(title, f"the name of section {column!r}", MAX_SECTION_TITLE)
    return out


def _string_list(value, what: str) -> list[str]:
    if value is None:
        return []
    if not isinstance(value, list) or not all(isinstance(v, str) for v in value):
        raise BoardToolError(f"{what} must be an array of strings.")
    if len(value) > 20:
        raise BoardToolError(f"{what} takes at most 20 entries.")
    return [v.strip() for v in value if v.strip()]


def _section_text(body: str, heading: str) -> str:
    span = _section_span(body, heading)
    return body[span[1]:span[2]] if span else ""


def _write_section(body: str, heading: str, text: str, *, replace: bool) -> str:
    """Replace or extend one `## ` section, leaving every other byte of the body alone."""
    block = text.rstrip("\n") + "\n"
    span = _section_span(body, heading)
    if span is None:
        prefix = body if body.endswith("\n") else body + "\n"
        return f"{prefix}\n## {heading}\n{block}"
    head, start, end = span
    existing = body[start:end]
    if replace:
        # A replace also settles the heading's spelling, which is how a card that still says
        # `## Request` comes out saying `## Issue` once its text is edited.
        line = f"## {heading}"
        if body[head:start].strip() != line:
            return body[:head] + line + "\n" + block + "\n" * _trailing_blanks(existing) + body[end:]
        return body[:start] + block + "\n" * _trailing_blanks(existing) + body[end:]
    kept = existing.rstrip("\n")
    joined = (kept + "\n" if kept else "") + block
    return body[:start] + joined + "\n" * _trailing_blanks(existing) + body[end:]


def _trailing_blanks(text: str) -> int:
    return len(text) - len(text.rstrip("\n")) - (1 if text.endswith("\n") else 0) if text.strip() else 1


def _rewrite_entry(what: str, old: str, new: str) -> str:
    """The thread block that makes a rewrite of the user's own text reversible (12.3)."""
    return (f"- ✦ rewrote {what}\n\n"
            f"<details><summary>before</summary>\n\n```\n{old.strip()}\n```\n\n</details>\n\n"
            f"<details><summary>after</summary>\n\n```\n{new.strip()}\n```\n\n</details>")


def _column_label(status: str) -> str:
    #: `ready` reads as "ready to ship" on its own (owner, 2026-09-19), so the label the pane
    #: shows is "Ready to start" and a thread line says the same words as the section header.
    #: The status id is unchanged, here and on disk.
    return {"inbox": "Inbox", "discussing": "Discussing", "planning": "Planning",
            "planned": "Planned", "ready": "Ready to start",
            "in-progress": "In progress", "needs-verification": "Needs verification",
            "needs-qa-llm": "Needs QA (LLM)",
            "needs-qa-human": "Needs QA (human)", "needs-review": "Needs review",
            "needs-labels": "Needs labels", "needs-ab": "Needs A/B", "deferred": "Deferred",
            "done": "Done", "dropped": "Dropped", "draft": "Draft", "approved": "Approved",
            "executing": "Executing", "active": "Active", "retired": "Retired"}.get(status, status)


#: The deterministic stage moves (#3XZV): what each stage event does to a work card's status.
#: Relay makes the move at the event itself, not on a model's judgment, and each one writes an
#: event entry to the thread saying why. The statuses a move starts *from* are listed — anywhere
#: else the card is further along (or closed, or not a work card) and the event leaves it be.
STAGE_MOVES = {
    # the thread's first non-event entry: the owner asked, or an agent commented
    "discussed": (("inbox",), "discussing", "the discussion started"),
    # a Plan turn was started on the card
    "plan-started": (("inbox", "discussing", "planning", "planned"), "planning",
                     "a Plan turn started"),
    # a Plan turn finished and left its `## Plan` on the card
    "plan-written": (("inbox", "discussing", "planning"), "planned", "the plan is on the card"),
}


def _task_items(raw, card: B.Card) -> list[B.TaskItem]:
    if not isinstance(raw, list) or len(raw) > 100:
        raise BoardToolError("tasks must be an array of at most 100 items.")
    existing = {i.item_id: i for i in card.tasks() if i.item_id}
    out: list[B.TaskItem] = []
    #: Position in `out` -> what blocks it, for the items that named something. An item that
    #: says nothing about `blocked_by` keeps whatever marker its line already carried.
    blockers: dict[int, list] = {}
    for index, entry in enumerate(raw, 1):
        if not isinstance(entry, dict):
            raise BoardToolError(f"task {index} must be an object.")
        text = _one_line(entry.get("text"), f"task {index} text", 300)
        status = str(entry.get("status") or "open").lower()
        if status not in B.ITEM_STATUSES:
            raise BoardToolError(f"task {index}: status must be one of {', '.join(B.ITEM_STATUSES)}.")
        item_id = entry.get("item_id")
        keep = existing.get(str(item_id).lower()) if item_id else None
        item = B.TaskItem(text=text, status=status,
                          item_id=keep.item_id if keep else None,
                          line=keep.line if keep else None,
                          depth=keep.depth if keep else 0,
                          card=keep.card if keep else None,
                          blocked_by=list(keep.blocked_by) if keep else [])
        if entry.get("card"):
            item.card = normalize_id(entry["card"], f"task {index} card")
        if entry.get("blocked_by") is not None:
            refs = entry["blocked_by"]
            if not isinstance(refs, list) or len(refs) > 20:
                raise BoardToolError(f"task {index}: blocked_by must be an array of at most "
                                     "20 references.")
            # A number is the 1-based position of another task in this same array (1-based
            # here, as `task {index}` is in every message of this file; 0-based inside
            # `board.set_task_blockers`). A string is an item id on this card, or `#K7Q2`.
            resolved = []
            for ref in refs:
                if isinstance(ref, bool) or not isinstance(ref, (int, str)):
                    raise BoardToolError(f"task {index}: blocked_by takes the number of another "
                                         "task in this list, an item id, or #CARD.")
                if isinstance(ref, int):
                    if not 1 <= ref <= len(raw):
                        raise BoardToolError(f"task {index}: blocked_by {ref} is not one of the "
                                             f"{len(raw)} tasks you sent.")
                    if ref == index:
                        raise BoardToolError(f"task {index} cannot block itself.")
                    ref -= 1
                resolved.append(ref)
            blockers[len(out)] = resolved
        out.append(item)
    if blockers:
        try:
            B.set_task_blockers(out, blockers)
        except B.BoardError as exc:
            raise BoardToolError(str(exc)) from exc
    return out


def _tracked_by_git(repo: Path, path: Path) -> bool:
    if not B.in_git_checkout(repo):
        # A board in Relay's data directory has no repository around it: nothing there is
        # committed, and running git would answer about an unrelated ancestor checkout.
        return False
    try:
        result = subprocess.run(["git", "-C", str(repo), "ls-files", "--error-unmatch", str(path)],
                                capture_output=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return False
    return result.returncode == 0


# ------------------------------------------------------------------------- policy

#: The parsed `board_policy.md`, keyed on what the file looked like when it was read (#GMCF): the
#: policy is ~5 KB of the system prompt and `policy_text` used to re-read and re-regex it on every
#: call. An edit to the file changes its mtime or its size, so a Board worked on in this
#: checkout still picks the new policy up on the next prompt build — no restart.
_POLICY_CACHE: tuple[tuple[int, int], str] | None = None


def policy_text() -> str:
    """The system-prompt block, versioned in `board_policy.md` so evals can pin it."""
    global _POLICY_CACHE
    path = Path(__file__).resolve().parent / "board_policy.md"
    try:
        stamp = path.stat()
        key = (stamp.st_mtime_ns, stamp.st_size)
        cached = _POLICY_CACHE
        if cached is not None and cached[0] == key:
            return cached[1]
        text = path.read_text(encoding="utf-8")
    except OSError:                                        # pragma: no cover - packaging slip
        return ""
    # The file's own provenance comment is for readers of the repository, not for the model.
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S).strip()
    _POLICY_CACHE = (key, text)
    return text


def prompt_section(tools: "BoardTools | None") -> str:
    """What `Agent.system_prompt` appends when this workspace has a Board.

    Only the part that is the same on every request of the conversation: what this pane holds
    changes as it works, so it is `session_note`, which the prompt carries at the end (#GMCF).
    """
    if tools is None or tools.autonomy == "off":
        return ""
    text = policy_text()
    if not text:
        return ""
    if tools.state == "uninitialized":
        return UNINITIALIZED_NOTE
    tabs = ", ".join(t for t in tools._tab_map())
    folder = tools.board.root.name
    header = (f"\n\nBoard: this project has one ({folder}/board.yaml). Tabs: {tabs}. "
              f"Autonomy: {tools.autonomy}"
              + (" — your card writes are proposals the user accepts in the Board pane."
                 if tools.autonomy == "suggest" else "") + "\n")
    return header + text


def session_note(tools: "BoardTools | None") -> str:
    """Your own token and the cards you hold, stated every turn (#R9G7), so the model never has to
    remember a session token or type one: `board_claim` fills both in from here.

    The one part of the prompt that changes while the conversation runs, so it goes at its very
    end: a provider's prompt cache and llama.cpp's prefix cache both key on the prefix, and a line
    that moves in the middle throws away everything after it (#GMCF, 2026-09-20).
    """
    if tools is None or tools.autonomy == "off" or tools.state == "uninitialized":
        return ""
    if not policy_text():
        return ""
    token = tools.pane_token or ""
    mine = (f"Your Board session: {token[:8]}." if token else "")
    if tools.claimed:
        mine += (" " if mine else "") + "You hold: " + ", ".join(f"#{c}" for c in tools.claimed) + "."
    return f"\n{mine}\n" if mine else ""
