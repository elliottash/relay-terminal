# SPDX-License-Identifier: GPL-3.0-or-later
"""Model-written session titles and tab labels (issue JRWQ).

The pane header and the tab label show what the pane is *doing*, not where it lives. One cheap
side call on the "chores" role writes the title after the first turn and then only when the work
has moved on (``REFRESH_TURNS`` turns, or a compaction); it never runs on every turn and never
when the user named the pane by hand. With no model configured, or when the call fails, the title
stays today's first-prompt text (``fallback_title``).

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
