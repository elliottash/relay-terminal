# SPDX-License-Identifier: AGPL-3.0-or-later
"""Model-written session titles and tab labels (issue JRWQ).

The pane header and the tab label show what the pane is *doing*, not where it lives. One cheap
side call on the "chores" role writes the title after the first turn and then only when the work
has moved on (``REFRESH_TURNS`` turns, or a compaction); it never runs on every turn and never
when the user named the pane by hand. With no model configured, or when the call fails, the title
stays today's first-prompt text (``fallback_title``).

Beside the title, the same machinery writes a *session summary*: two or three sentences (what was
wanted, what was done, what is left) for the session list and the resume picker, from a bounded
digest of the conversation. It rides the title's cadence, so it costs one extra cheap call at the
same moments and never one per turn.

Tab labels cost no extra title call: they are derived from the pane titles the panes already have.
Panes on the same work give one phrase, unrelated ones are joined with "; ". The "same work or
not" judgement is another chores call; ``related_text`` is the offline answer and is mirrored in
``src/PaneTitles.cpp`` so the GUI can label a tab before, or without, any model.
"""
from __future__ import annotations

import re

from . import sidecall

# ~6 words, per the issue. The cap is characters so a title never breaks a header.
MAX_TITLE = 60
MAX_WORDS = 6
# The user's own name for a pane: longer, still bounded (sessions.meta title field).
MAX_USER_TITLE = 200
# Turns between two automatic refreshes. Turn 1 always gets a title; after that the work has to move on.
REFRESH_TURNS = 5
# A title is a handful of words, but the budget also has to cover a reasoning model's hidden
# tokens: too tight and the reply comes back truncated, which providers report as an error.
MAX_TOKENS = 1024
MAX_LABEL = 80

TITLE_SYSTEM = (
    "You name a coding session for a terminal tab header. Reply with JSON only: "
    '{"title": "..."}. The title says what the session is working on, at most six words, '
    "no quotes, no trailing period, no file paths unless they are the point, sentence case, "
    'starting with a verb where it reads naturally ("Fixing pane drag and Ctrl+H sizing", '
    '"Release notes for 0.1"). Never mention the assistant, the user or the word "session".'
)

LABEL_SYSTEM = (
    "You label a terminal tab that holds several panes. You are given one short title per pane. "
    "Decide whether the panes are on the same piece of work. Reply with JSON only: "
    '{"related": true|false, "label": "..."}. When they are related, the label is one phrase of at '
    "most six words covering all of them. When they are not, leave the label empty and Relay joins "
    "the pane titles itself. Never invent work that the titles do not mention."
)

# Words that say nothing about which work a pane is on.
STOPWORDS = frozenset("""
a an and are as at be being by for from in into is it its of on onto or over than that the their then
this to up via with without new old more less some any all other another use using used make making
work working fix fixing add adding update updating change changing run running write writing set setting
""".split())


def fallback_title(prompt: str) -> str:
    """Today's title: the first prompt, collapsed, at most 80 characters."""
    return " ".join(str(prompt or "").split())[:80]


def clean(title, limit: int = MAX_TITLE, max_words: int = MAX_WORDS) -> str:
    """A model reply (or a user's typing) reduced to a header-sized phrase."""
    text = " ".join(str(title or "").split())
    text = text.strip().strip("\"'`“”‘’")
    # Models like to answer "Title: Fixing pane drag".
    text = re.sub(r"^(?:title|label)\s*[:\-—]\s*", "", text, flags=re.I).strip()
    text = text.rstrip(".").strip()
    if max_words:
        words = text.split()
        if len(words) > max_words:
            text = " ".join(words[:max_words])
    return sidecall.clip(text, limit)


def due(turns: int, source: str, title_turn: int, stale: bool) -> bool:
    """Whether an automatic refresh is owed right now.

    Never for a hand-set name, never before the first turn finishes, always right after that first
    turn, then only when the work has moved on: ``REFRESH_TURNS`` further turns, or a compaction
    (``stale``) since the last one.
    """
    if source == "user" or turns <= 0:
        return False
    if title_turn <= 0:
        return True
    if turns <= title_turn:          # rewound below the last title: nothing new to name
        return False
    return bool(stale) or turns - title_turn >= REFRESH_TURNS


def generate(provider, messages: list[dict], cancel=None) -> str:
    """One no-tools call on the chores role. Returns "" when the model gives nothing usable."""
    transcript = sidecall.render_transcript(messages, max_chars=12_000, per_message=1200)
    if not transcript.strip():
        return ""
    text, _ = sidecall.call(provider, TITLE_SYSTEM, "Conversation:\n\n" + transcript, cancel)
    data = sidecall.parse_json_object(text) or {}
    candidate = data.get("title") if isinstance(data.get("title"), str) else text
    return clean(candidate)


# ----- session summaries ---------------------------------------------------------------------
# The title says what a pane is doing in six words; the summary says what happened in two or
# three sentences, for the session list and the resume picker. Same role (chores -> Lite), same
# cadence points, one extra cheap call.
MAX_SUMMARY = 320
# Turns between two automatic summaries. The first one follows the first assistant reply.
SUMMARY_REFRESH_TURNS = 5
# The digest handed to the model is capped here whatever the conversation's size.
SUMMARY_DIGEST_CHARS = 6000
SUMMARY_MAX_TOKENS = 1024
# What one summary costs, for conversations_summarize_estimate: ~320 characters of prose out.
SUMMARY_OUTPUT_TOKENS = 110
CHARS_PER_TOKEN = 4
# Messages of the conversation's tail that go into the digest, and their caps.
DIGEST_RECENT = 8
DIGEST_MESSAGE_CHARS = 900
DIGEST_FIRST_CHARS = 800
DIGEST_CARRIED_CHARS = 1200
DIGEST_FILES = 20
DIGEST_TODOS = 8
# Todo statuses that are still work (todos.OPEN, kept here so titles.py stays importable alone).
OPEN_TODO = ("pending", "in_progress", "blocked")

SUMMARY_SYSTEM = (
    "You write the standing summary of a coding session for a list of past sessions. Reply with "
    'JSON only: {"summary": "..."}. Two or three short sentences, at most 320 characters in all, '
    "plain text: no markdown, no bullets, no headings, no quotes. Write it like a commit message: "
    "what the person wanted, then what was done, then what is left or where it stopped, starting "
    'the last sentence with "Left:" when something is unfinished. Never begin with "The user", '
    '"This session" or "Summary"; never mention the assistant, the model or the conversation '
    'itself. Example: "Fix the FTS index going stale. Added reconcile on first use and an autosave '
    'hook; symlink digests unified. Left: backfill old summaries."'
)

# "Here is a summary:", "Summary - ", "The user wanted to ..." and friends.
_PREAMBLE = re.compile(
    r"^(?:here(?:'s| is)[^:]{0,60}:"
    r"|(?:session |conversation )?summary\s*[:\-—]"
    r"|(?:the user|the person|this session|the session|this conversation)\s+"
    r"(?:wanted|asked for|asked|requested|is about|was about)\s+(?:to\s+)?)\s*",
    re.I)
_BULLET = re.compile(r"^[ \t]*(?:[-*+•>]+[ \t]+|\d+[.)][ \t]+|#{1,6}[ \t]*)", re.M)
_LINK = re.compile(r"\[([^\]\n]{1,120})\]\([^)\n]*\)")
_FENCE = re.compile(r"```[a-zA-Z0-9_+-]*")


def clean_summary(text, limit: int = MAX_SUMMARY) -> str:
    """A model reply reduced to the plain prose the session list shows.

    Strips code fences, markdown emphasis, bullets, headings and links, drops the quotes and the
    "Here is a summary:" / "The user wanted to" preambles models like, collapses whitespace and
    cuts on a sentence boundary at ``limit``.
    """
    body = str(text or "")
    body = _FENCE.sub(" ", body)
    body = _LINK.sub(r"\1", body)
    body = _BULLET.sub("", body)
    body = body.replace("**", "").replace("__", "").replace("`", "").replace("*", "")
    body = " ".join(body.split())
    for _ in range(3):
        stripped = body.strip().strip("\"'“”‘’")
        stripped = _PREAMBLE.sub("", stripped, count=1).strip()
        if stripped == body:
            break
        body = stripped
    if not body:
        return ""
    if body[:1].isalpha() and body[:1].islower():
        body = body[0].upper() + body[1:]
    return _cut_sentence(body, limit)


def _cut_sentence(text: str, limit: int) -> str:
    """``text`` at ``limit``, ending on a sentence where one is close enough to the cap."""
    if len(text) <= limit:
        return text
    head = text[:limit]
    end = max(head.rfind(". "), head.rfind("! "), head.rfind("? "))
    if end < 0 and head.rstrip().endswith((".", "!", "?")):
        end = len(head.rstrip()) - 1
    if end >= limit * 0.5:
        return head[:end + 1].rstrip()
    return sidecall.clip(text, limit)


def summary_due(turns: int, summary_turn: int, stale: bool, has_reply: bool = True) -> bool:
    """Whether a fresh summary is owed right now.

    Nothing to summarise before the first assistant reply; then always; then only when the work
    has moved on: ``SUMMARY_REFRESH_TURNS`` further turns, or a compaction (``stale``).
    """
    if turns <= 0 or not has_reply:
        return False
    if summary_turn <= 0:
        return True
    if turns <= summary_turn:          # rewound below the last summary: nothing new happened
        return False
    return bool(stale) or turns - summary_turn >= SUMMARY_REFRESH_TURNS


def has_reply(messages) -> bool:
    """Whether the conversation has anything a summary could be about."""
    return any(isinstance(m, dict) and m.get("role") == "assistant"
               and (m.get("content") or m.get("tool_calls")) for m in messages or [])


def touched_files(checkpoint_items, limit: int = DIGEST_FILES) -> list[str]:
    """Paths this conversation wrote, oldest turn first, without repeats."""
    out, seen = [], set()
    for item in checkpoint_items or []:
        if not isinstance(item, dict):
            continue
        for path in item.get("files") or {}:
            if path not in seen:
                seen.add(path)
                out.append(str(path))
                if len(out) >= limit:
                    return out
    return out


def _message_text(message: dict, cap: int) -> str:
    """One message as a line: its text, capped, with the tools it called named after it.

    The tool names go on after the cap, not before it: which tools ran says more about the turn
    than the last 40 characters of a long reply, and a cut must not swallow them.
    """
    content = " ".join(str(message.get("content") or "").split())
    names = [c.get("function", {}).get("name") for c in message.get("tool_calls") or [] if isinstance(c, dict)]
    names = [n for n in names if isinstance(n, str) and n]
    suffix = (" [tools: " + ", ".join(names[:8]) + "]") if names else ""
    return (sidecall.clip(content, max(40, cap - len(suffix))) + suffix).strip()


def digest(messages, *, files=(), todos=(), max_chars: int = SUMMARY_DIGEST_CHARS) -> str:
    """The bounded input one summary call gets: the first request, the last compaction summary,
    the tail of the conversation, the files touched and the todos still open. Never longer than
    ``max_chars``, whatever the conversation's size."""
    items = [m for m in messages or [] if isinstance(m, dict) and m.get("role") in ("user", "assistant")]
    head, tail = [], []
    prompts = [m for m in items
               if m.get("role") == "user" and m.get("relay_kind") not in ("note", "summary", "carried")]
    if prompts:
        head.append("First request:\n" + _message_text(prompts[0], DIGEST_FIRST_CHARS))
    carried = [m for m in items if m.get("relay_kind") == "summary"]
    if carried:
        head.append("Earlier work (compaction summary):\n" + _message_text(carried[-1], DIGEST_CARRIED_CHARS))
    for name, values, cap in (("Files touched", files, DIGEST_FILES), ("Open todos", todos, DIGEST_TODOS)):
        picked = [" ".join(str(v).split()) for v in list(values or ())[:cap] if str(v or "").strip()]
        if picked:
            tail.append(sidecall.clip(f"{name}: " + ", ".join(picked), 400))
    budget = max_chars - sum(len(part) + 2 for part in head + tail) - 40
    recent = []
    for message in reversed(items[-DIGEST_RECENT:]):
        chunk = ("User: " if message.get("role") == "user" else "Assistant: ") + _message_text(
            message, DIGEST_MESSAGE_CHARS)
        if len(chunk) + 2 > budget:
            break
        budget -= len(chunk) + 2
        recent.insert(0, chunk)
    parts = head + (["Most recent messages:\n" + "\n".join(recent)] if recent else []) + tail
    return "\n\n".join(parts)[:max_chars]


def saved_inputs(data: dict) -> dict:
    """``{messages, files, todos}`` for one saved session file (sessions.SessionStore.load)."""
    messages = [m for m in (data.get("messages") or []) if isinstance(m, dict)]
    checkpoints = (data.get("checkpoints") or {}).get("items") or []
    todos = [t.get("text") for t in ((data.get("todos") or {}).get("items") or [])
             if isinstance(t, dict) and t.get("status") in OPEN_TODO and t.get("text")]
    return {"messages": messages, "files": touched_files(checkpoints), "todos": todos[:DIGEST_TODOS]}


def approx_tokens(chars: int) -> int:
    """A token count for an estimate the user sees. Nothing is billed on it."""
    return max(0, int(chars) // CHARS_PER_TOKEN)


def generate_summary(provider, messages, cancel=None, *, files=(), todos=()) -> str:
    """One no-tools call on the chores role. "" when the model gives nothing usable, which the
    caller reads as "keep the summary there already is"."""
    body = digest(messages, files=files, todos=todos)
    if not body.strip():
        return ""
    text, _ = sidecall.call(provider, SUMMARY_SYSTEM, "Session so far:\n\n" + body, cancel)
    data = sidecall.parse_json_object(text)
    # A reply that is JSON but not *this* JSON answered some other question: its braces are not a
    # summary, and storing them would put a model's stray object in the session list.
    candidate = (data.get("summary") if isinstance(data.get("summary"), str) else "") if data is not None else text
    summary = clean_summary(candidate)
    # A word or two is not a summary: junk keeps the previous one rather than replacing it.
    return summary if len(summary) >= 12 and any(c.isalpha() for c in summary) else ""


# ----- tab labels ---------------------------------------------------------------------------
def distinct(titles) -> list[str]:
    """Non-empty pane titles, collapsed, in order, without repeats (case-insensitive)."""
    out, seen = [], set()
    for title in titles or []:
        text = " ".join(str(title or "").split())
        if not text or text.lower() in seen:
            continue
        seen.add(text.lower())
        out.append(text)
    return out


def _words(title: str) -> set[str]:
    return {w for w in re.findall(r"[a-z0-9]+", title.lower()) if len(w) > 2 and w not in STOPWORDS}


def related_text(titles: list[str]) -> bool:
    """The offline "same work?" answer: every pair of titles shares a content word.

    Mirrored in src/PaneTitles.cpp, which the GUI uses when no model is configured.
    """
    items = [t for t in distinct(titles)]
    if len(items) < 2:
        return True
    sets = [_words(t) for t in items]
    if any(not s for s in sets):
        return False
    return all(sets[0] & other for other in sets[1:])


def join(titles: list[str], related: bool, phrase: str = "", limit: int = MAX_LABEL) -> str:
    """The tab label: one phrase for related panes, otherwise the titles joined with "; "."""
    items = distinct(titles)
    if not items:
        return ""
    if related:
        text = clean(phrase) if phrase else items[0]
        return sidecall.clip(text or items[0], limit)
    return sidecall.clip("; ".join(items), limit)


def label(provider, titles, cancel=None) -> dict:
    """Chores-role "same work or not" check over the pane titles. Falls back to related_text()."""
    items = distinct(titles)
    if len(items) < 2:
        return {"label": join(items, True), "related": True, "source": "text"}
    if provider is None:
        return {"label": join(items, related_text(items)), "related": related_text(items), "source": "text"}
    text, _ = sidecall.call(provider, LABEL_SYSTEM, "Pane titles:\n" + "\n".join(f"- {t}" for t in items), cancel)
    data = sidecall.parse_json_object(text) or {}
    related = data.get("related")
    if not isinstance(related, bool):
        related = related_text(items)
    phrase = data.get("label") if isinstance(data.get("label"), str) else ""
    return {"label": join(items, related, phrase), "related": related, "source": "model"}
