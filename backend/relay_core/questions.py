# SPDX-License-Identifier: GPL-3.0-or-later
"""`ask_user`: the agent asks the user a question and waits for the answer.

Card #MQ9C. Plan mode had no way to reach the user between "investigate" and `write_plan`, so a
planner that was unsure guessed. Every other harness asks during planning (the card quotes
opencode's and Warp's shapes word for word); this is Relay's.

Like `type_into_program` (`program_input.py`) it is a round trip, because the worker cannot draw
anything: the worker emits `question`, the pane draws the card, the user answers, the pane replies
`question_answer` and the waiting turn thread wakes. The turn loop and the protocol loop are
different threads (`backend/worker.py`), so blocking one is safe.

What it does *not* share with `type_into_program`:

* **No reply deadline.** A question is asked of a person who may be at lunch. Only Stop, a new
  prompt, or the pane going away ends the wait — never a timer, which would silently turn "the user
  is thinking" into "the tool failed" and send the model off guessing again.
* **No consent to check.** Asking is always allowed; it is answering that is the user's to refuse.
  A skipped question comes back as `Unanswered`, and the model is told to carry on with its own
  judgment rather than ask the same thing again.

A question may be a **multiple choice** or **open** — `options` is optional (owner, 2026-09-19:
"dont force multiple choice -- allow open-ended questions"). Options are for a decision with known
branches, where naming them is a kindness and a number is the whole answer; an open question is for
the ones a list would falsify ("what should the error message say?"). A model that invents two
options to satisfy a schema asks a worse question than one that just asks.

Both modes carry it (owner, 2026-09-19: "let the non-plan agent use the questions as well (like
warp / claude)"), which is what opencode does for its `build` and `plan` agents and what Warp does
with a per-profile permission. A subagent never gets it: it cannot see the pane and the user has no
idea it exists (`subagents.RestrictedExecutor`).

Protocol: docs/AGENT-SESSIONS-PROTOCOL.md section 27.
"""
from __future__ import annotations

import threading
import uuid
from typing import Callable

from .provider import Cancelled

MAX_QUESTIONS = 4          # one screenful; a model that needs more should ask again after the answers
MAX_OPTIONS = 5
MIN_OPTIONS = 2            # one option is not a question
MAX_HEADER = 30            # the chip over the card, e.g. "Scope"
MAX_QUESTION = 300
MAX_LABEL = 60
MAX_DESCRIPTION = 200
MAX_ANSWER = 4000          # a typed answer may be a paragraph; only a runaway paste is cut
ANSWER_CUT = " […truncated]"   # never cut silently: the model must see that words are missing
MAX_ASKS_PER_TURN = 6      # stops a loop from interviewing the user
CUSTOM_LABEL = "Type your own answer"
UNANSWERED = "Unanswered"
SKIP_WORD = "/skip"        # what the pane types to leave a question unanswered

def _call_id() -> str:
    """A card's id. Random rather than counted: a counter restarts at `q-1` in every worker
    process, so a pane that outlived a worker could answer a new card with an old card's id and be
    believed. Nothing reads the number, so there is nothing to lose by it being unguessable."""
    return f"q-{uuid.uuid4().hex}"

SPEC = {
    "type": "function",
    "function": {
        "name": "ask_user",
        "description": (
            "Ask the user a question and wait for the answer. For what the code cannot tell you: "
            "which direction they want, how far a change goes, a trade-off worth their opinion, "
            "wording only they can choose. Give `options` when the decision really has a few known "
            "branches — then a number is the whole answer — and leave them out for an open "
            "question rather than inventing choices to fill the field. In plan mode ask before you "
            "write the plan. Do not ask permission to use a tool, and do not ask whether your plan "
            "is good — write it and let them edit it. The turn stops until they answer: ask "
            "everything in one call."),
        "parameters": {
            "type": "object",
            "properties": {
                "questions": {
                    "type": "array",
                    "description": f"Up to {MAX_QUESTIONS}, asked together.",
                    "items": {
                        "type": "object",
                        "properties": {
                            "header": {"type": "string",
                                       "description": f"Two or three words naming the decision, "
                                                      f"max {MAX_HEADER} characters."},
                            "question": {"type": "string", "description": "The question, in one sentence."},
                            "options": {
                                "type": "array",
                                "description": f"Optional: {MIN_OPTIONS}-{MAX_OPTIONS} answers to choose "
                                               f"between. Omit for an open question. No \"Other\" or "
                                               f"catch-all option: \"{CUSTOM_LABEL}\" is always offered.",
                                "items": {
                                    "type": "object",
                                    "properties": {
                                        "label": {"type": "string", "description": "The answer, in 1-5 words."},
                                        "description": {"type": "string", "description": "One line on what it means."},
                                        "recommended": {"type": "boolean",
                                                        "description": "Your pick. At most one per question."},
                                    },
                                    "required": ["label", "description"],
                                    "additionalProperties": False,
                                },
                            },
                            "multiple": {"type": "boolean", "description": "Let them pick more than one."},
                        },
                        "required": ["header", "question"],
                        "additionalProperties": False,
                    },
                },
            },
            "required": ["questions"],
            "additionalProperties": False,
        },
    },
}

def _text(value, field: str, limit: int) -> str:
    if not isinstance(value, str) or not value.strip() or "\x00" in value:
        raise ValueError(f"{field} must be text.")
    clean = " ".join(value.split())
    if len(clean) > limit:
        raise ValueError(f"{field} must be at most {limit} characters.")
    return clean


def validate(args) -> list[dict]:
    """The model's `ask_user` arguments, as the pane will draw them. Raises ValueError with a
    sentence the model can act on — it is shown the error and gets to call again."""
    if not isinstance(args, dict) or set(args) - {"questions"}:
        raise ValueError("ask_user takes one argument, questions.")
    items = args.get("questions")
    if not isinstance(items, list) or not items:
        raise ValueError("questions must be a list of at least one question.")
    if len(items) > MAX_QUESTIONS:
        raise ValueError(f"Ask at most {MAX_QUESTIONS} questions in one call.")
    questions = []
    for index, item in enumerate(items, 1):
        if not isinstance(item, dict) or set(item) - {"header", "question", "options", "multiple"}:
            raise ValueError(f"Question {index} has an unknown field.")
        # No options is an open question: the user types the answer. One option is neither, and is
        # the shape a model reaches for when it means "is this all right?" — which is not a question.
        options = item.get("options", [])
        if options is None:
            options = []
        if not isinstance(options, list) or (options and not MIN_OPTIONS <= len(options) <= MAX_OPTIONS):
            raise ValueError(f"Question {index}: give {MIN_OPTIONS} to {MAX_OPTIONS} options, or none "
                             "at all for an open question.")
        multiple = item.get("multiple", False)
        if type(multiple) is not bool:
            raise ValueError(f"Question {index}: multiple must be true or false.")
        if multiple and not options:
            raise ValueError(f"Question {index}: multiple needs options to choose between.")
        built, labels, recommended = [], set(), False
        for option in options:
            if not isinstance(option, dict) or set(option) - {"label", "description", "recommended"}:
                raise ValueError(f"Question {index} has an option with an unknown field.")
            label = _text(option.get("label"), f"Question {index}: option label", MAX_LABEL)
            if label.casefold() == CUSTOM_LABEL.casefold():
                raise ValueError(f"Question {index}: \"{CUSTOM_LABEL}\" is offered already; "
                                 "list only real choices.")
            if label.casefold() in labels:
                raise ValueError(f"Question {index} offers {label!r} twice.")
            labels.add(label.casefold())
            entry = {"label": label,
                     "description": _text(option.get("description"), f"Question {index}: option description",
                                          MAX_DESCRIPTION)}
            # The type check comes first, as `multiple`'s does: `recommended: 0` or `""` is a
            # model getting the schema wrong, and letting it through as "not recommended" hides
            # that from the one reader who can fix it.
            flag = option.get("recommended", False)
            if type(flag) is not bool:
                raise ValueError(f"Question {index}: recommended must be true or false.")
            if flag:
                if recommended:
                    raise ValueError(f"Question {index} recommends two options; recommend at most one.")
                recommended = True
                entry["recommended"] = True
            built.append(entry)
        questions.append({"header": _text(item.get("header"), f"Question {index}: header", MAX_HEADER),
                          "question": _text(item.get("question"), f"Question {index}: question", MAX_QUESTION),
                          "options": built, "multiple": multiple})
    return questions


def preview(questions: list[dict]) -> str:
    """The tool-call preview the pane prints before the card, like every other tool's."""
    lines = []
    for question in questions:
        lines.append(f"{question['header']}: {question['question']}")
        lines += [f"  · {option['label']}" for option in question["options"]]
        if not question["options"]:
            lines.append("  · (open)")
    return "ASK THE USER\n\n" + "\n".join(lines)


def clip_answer(text: str) -> str:
    """One answer, capped. A typed answer is prose — "what should the error message say?" can
    fairly be answered with a paragraph — so the cap is `MAX_ANSWER`, not a few labels' worth. A
    cut says so in the text the model reads: an answer that stops mid-sentence without a marker
    reads as the user changing their mind, and the model acts on half a sentence."""
    if len(text) <= MAX_ANSWER:
        return text
    return text[:MAX_ANSWER - len(ANSWER_CUT)] + ANSWER_CUT


def answer_text(answers) -> str:
    """One question's answer as the model reads it: the chosen labels, the user's own words, or
    `Unanswered`. Anything the pane sends is the user's text, so it is clipped, never trusted."""
    if not isinstance(answers, list) or not answers:
        return UNANSWERED
    chosen = [clip_answer(" ".join(str(a).split())) for a in answers if str(a).strip()]
    return ", ".join(chosen) if chosen else UNANSWERED


def wake_on_set(event: threading.Event, callback: Callable[[], None]) -> bool:
    """Run `callback` whenever `event` is set, and say whether that could be arranged.

    A card has no deadline, so the wait under it is the one wait in the worker that can last hours.
    `threading.Event` has no way to register a waiter and the cancel event is the agent's own
    (`Agent.cancel_event`), so the alternative is a timer: the first version of this file woke
    twenty times a second for as long as a question was on screen, to ask an event that had not
    changed. This wraps `set` on that one instance — the real `set` runs first, so every other
    waiter sees exactly what it saw before — and falls back to `False` for anything that is not a
    plain Python event, which the caller answers by polling as before.
    """
    try:
        hooks = getattr(event, "_relay_wake_hooks", None)
        if hooks is None:
            hooks = []
            plain = event.set

            def set_and_wake() -> None:
                plain()
                for hook in list(hooks):
                    hook()

            event._relay_wake_hooks = hooks
            event.set = set_and_wake
        hooks.append(callback)
        return True
    except (AttributeError, TypeError):   # pragma: no cover - not a plain threading.Event
        return False


class Questions:
    """Per-pane state plus the blocking round trip. One instance lives on the pane's ToolExecutor."""

    def __init__(self, emit: Callable[[dict], None], cancel: threading.Event):
        self.emit = emit
        self.cancel = cancel
        self._lock = threading.Lock()
        self._pending: dict[str, list] = {}
        self.asks = 0
        # Stop is what ends a wait the user never ends, so it has to wake the wait rather than be
        # noticed by it. Hooked here and not at the first question, so that anything which takes
        # its own reference to `cancel.set` afterwards takes the hooked one.
        self._hooked = wake_on_set(cancel, lambda: self.fail_pending("cancelled"))

    # ----- turn boundaries -------------------------------------------------------------------
    def begin_turn(self) -> None:
        self.asks = 0

    def end_turn(self) -> None:
        """A turn cannot end with a card still up: the user would be answering nobody."""
        self.asks = 0
        self.fail_pending("cancelled")

    # ----- the tool --------------------------------------------------------------------------
    def tool_spec(self) -> dict:
        return SPEC

    def prepare(self, args: dict) -> tuple[dict, str]:
        questions = validate(args)
        return {"questions": questions}, preview(questions)

    def execute(self, payload: dict, turn_id=None) -> dict:
        if self.cancel.is_set():
            raise Cancelled("Stopped.")
        questions = payload["questions"]
        if self.asks >= MAX_ASKS_PER_TURN:
            return {"ok": False, "refused": "cap",
                    "error": (f"You have asked the user {MAX_ASKS_PER_TURN} times this turn, which is the "
                              "limit. Decide with what you know, say in your reply which way you went and "
                              "why, and let them correct you.")}
        call_id = _call_id()
        done = threading.Event()
        with self._lock:
            self._pending[call_id] = [done, None]
            self.asks += 1
        self.emit({"event": "question", "id": call_id, "turn_id": turn_id, "questions": questions})
        # Stop between the check at the top of this method and the card being registered above
        # would otherwise be a Stop that woke nothing: the hook ran when there was no card yet.
        if self.cancel.is_set():
            self.fail_pending("cancelled")
        # No deadline: the user may be away, and a timeout would report "failed" for "still thinking".
        # The wait is woken — by the answer, by Stop, or by the turn ending — and not timed, because
        # a question may sit on screen for hours.
        if self._hooked:
            done.wait()
        else:                                            # pragma: no cover - fallback, see wake_on_set
            while not done.wait(0.25):
                if self.cancel.is_set():
                    self.fail_pending("cancelled")
        reply = self._take(call_id) or {}
        if reply.get("code") == "cancelled":
            # Stop, or the turn ending under the card: the pane takes it down either way, so it is
            # never left up for a turn that no longer exists.
            self.emit({"event": "question_closed", "id": call_id, "reason": "cancelled"})
            raise Cancelled("Stopped.")
        return self._result(questions, reply)

    def _result(self, questions: list[dict], reply: dict) -> dict:
        given = reply.get("answers") if isinstance(reply.get("answers"), list) else []
        answers = []
        for index, question in enumerate(questions):
            answers.append({"header": question["header"], "question": question["question"],
                            "answer": answer_text(given[index] if index < len(given) else None)})
        unanswered = [a for a in answers if a["answer"] == UNANSWERED]
        result = {"ok": True, "answers": answers,
                  "summary": "The user answered: "
                             + "; ".join(f"{a['question']} → {a['answer']}" for a in answers)}
        if len(unanswered) == len(answers):
            result["note"] = ("The user did not answer. Do not ask again: decide with what you know, "
                              "say in your reply which way you went and why, and let them correct you.")
        elif unanswered:
            result["note"] = ("Some questions were left unanswered. Decide those yourself and say so; "
                              "do not ask them again.")
        return result

    # ----- replies from the GUI ----------------------------------------------------------------
    def resolve(self, message: dict) -> None:
        """The pane's `question_answer`. Unknown ids are ignored: a card answered after its turn
        was stopped is the user's click landing late, not an error to report."""
        if not isinstance(message, dict):
            raise ValueError("question_answer must be an object.")
        call_id = message.get("id")
        with self._lock:
            slot = self._pending.get(call_id)
            if slot is None:
                return
            slot[1] = {k: v for k, v in message.items() if k not in ("type", "id")}
            slot[0].set()

    def fail_pending(self, code: str) -> None:
        with self._lock:
            slots = list(self._pending.values())
        for slot in slots:
            if slot[1] is None:
                slot[1] = {"code": code}
            slot[0].set()

    def _take(self, call_id: str):
        with self._lock:
            slot = self._pending.pop(call_id, None)
        return slot[1] if slot else None
