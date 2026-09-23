# SPDX-License-Identifier: AGPL-3.0-or-later
"""What an agent is *about*: the `context` block of `configure` (protocol 33, card #AGNT).

The owner, 2026-09-20: *"an agent interface is the prompt box. it has a set of options and tools
that vary according to the setting/task, but in general they are shared systems"*, and *"agents
are specialized for the given pane context, but the general rule/approach is that agents have
access to all systems and can work across panes and contexts"*.

Read together those two sentences fix the seam, and this module is the worker's half of it.  A
**context** says what the agent is about — its name, the role it answers on, the brief in front
of it, where its conversation is kept, whether the surface has a shell, how the composer routes
— and it deliberately carries **no tool whitelist**.  What tools an agent holds is a *scope*
(`pane`, `console`, `card`), named here and resolved in exactly one place (`Agent.tools`), never
inferred from whether a board happens to be attached: inferring it is what gave a board-less
helper the full executor by accident while a boarded one got read-only tools (`agent.py`, the
`card_scope` branch, before this card).

`ContextSpec` is the exact bytes of that block, so the C++ half (`src/AgentContext.h`,
`ContextSpec::toJson`) and this one are tested against one shape.  Nothing here imports the
agent, the provider or the tool executor: it is a wire format and a few briefs.
"""
from __future__ import annotations

import hashlib
from dataclasses import dataclass
from pathlib import Path

from . import sessions as S

#: The contexts that exist today.  A new surface adds a name here and a `Context` in the GUI;
#: nothing else in the worker is per surface.  An unknown name is refused rather than ignored,
#: because a typo would otherwise silently take the terminal's defaults.
NAMES = ("terminal", "switchboard", "card", "options", "actions", "sessions", "projects", "globals",
         "models")

#: The **named** tool scopes.  One agent, one scope, resolved once:
#:
#: * ``pane``    — a terminal pane's own agent: the whole executor, as it always was.
#: * ``console`` — an agent console that is not a terminal (the Board page, Options,
#:   Actions, Sessions): the whole executor *plus* the board tools' chat set and the app tools.
#:   It has the shell and the file tools since the owner's decision of 2026-09-20 — a context
#:   specialises an agent without fencing it, and the gates are the ones he chose (the
#:   `agent_safe` / `settable` markers and the Options › Agent toggle).
#:
#: A card's Discuss or Plan turn was a third scope, ``card``, until card #CTRN: the mode's board
#: tools and the read-only file tools, and nothing else.  A card turn is an ordinary console
#: turn now, and 19.20 — a rule about the *stage*, never a fence around the surface — is
#: refused at call time instead (`Agent.set_card_turn`, `board_tools.CardScope.refusal`).
SCOPES = ("pane", "console")

#: Scopes a GUI may still name that no longer exist, and what they mean now.  A `configure` that
#: names one is answered rather than refused, for the release it takes a GUI to catch up.
RETIRED_SCOPES = {"card": "console"}

#: What the composer does with a line that is not obviously a prompt.  The terminal is the only
#: context that can run it as a command, so it is the only one that says `auto`.
ROUTINGS = ("auto", "agent")

#: Where the conversation is kept.  `""` is the agent's own default session store (a pane);
#: `helper` is the per-(project, tab) file of 30.7, whose layout is unchanged by this card.
PERSIST_SCOPES = ("", "pane", "helper")

MAX_NAME = 32
MAX_KEY = 128
MAX_TITLE = 200
MAX_BRIEF_KEY = 64
MAX_WORKSPACE = 4096

#: How much of "what is on screen" one turn may carry (`ask {screen}`, and the context's own
#: standing line).  It is a hint, not a context dump: the agent reads the rows live with the app
#: tools, because a settings list pasted into a prompt is stale the moment the person changes one.
MAX_SCREEN = 2000

#: How long a `surface` may be.  It names *which console* asked, so that several consoles can
#: share one conversation and each still know which of its own asks an event belongs to; it is
#: free text (a tab id, `card:AGNT`), never an enum, because the GUI mints them.
MAX_SURFACE = 64


# ---------------------------------------------------------------------------------------------
# Where a context's conversation lives.  Moved here from `board_chat` with the file layout
# untouched (#FEJQ's `3ddd2193`): the same tab in the same project must resolve to the same file
# at every start, so a restart brings each tab's console back with its own history.

#: `$XDG_DATA_HOME/relay/helper-sessions/<workspace digest>/<tab digest>.json`, deliberately
#: *outside* `relay/sessions/` — the one tree `SessionStore.index()` indexes.  A console's
#: chatter is not one of the person's own sessions and is not listed as one (14).
HELPER_DIRNAME = "helper-sessions"


def helper_dir(workspace) -> Path:
    """The directory this workspace's console conversations live in."""
    beside = S.default_session_dir(workspace or "")
    return beside.parent.parent / HELPER_DIRNAME / beside.name


def helper_file(workspace, key: str) -> tuple[Path, str]:
    """(directory, session id) for this console's conversation, wherever it already is.

    **The backend owns "a tab's conversation does not move because its project arrived."** The
    directory is the workspace's and the file name is the tab's, which files a project's consoles
    together — but the *name* is already unique, so the directory is filing and nothing more. A
    tab that is asked something before its project is known, or that gains one later, changed its
    workspace digest and left its history behind: the live drive of card #AGNT read one tab id
    under two digests, and a restart came back to an empty console.

    So a conversation that already exists under **any** digest for this key is the one that is
    opened, and only a genuinely new one is filed under the workspace of the moment. The GUI's
    half of the bargain is to send the tab's project as soon as it knows it
    (`RelayWindow::TabConsoleContext::spec`, which reads it fresh on every `configure`); this is
    what makes the day it did not know harmless rather than lossy.
    """
    name = helper_session_id(key)
    here = helper_dir(workspace or "")
    if (here / f"{name}.json").exists():
        return here, name
    root = here.parent
    try:
        for digest in sorted(p for p in root.iterdir() if p.is_dir()):
            if (digest / f"{name}.json").exists():
                return digest, name
    except OSError:
        pass
    return here, name


def helper_session_id(key: str) -> str:
    """The session id a persistence key always has: 32 hex digits from the key.

    Derived rather than stored, so nothing has to be written down to find the conversation
    again.  The personalisation string is the one #FEJQ chose, so files written before this card
    are found by the same name afterwards.
    """
    return hashlib.blake2b(str(key).encode("utf-8"), digest_size=16,
                           person=b"relay-helper").hexdigest()


# ---------------------------------------------------------------------------------------------
# The briefs.  One paragraph per context: what the surface shows, what can be done there, and
# the rule that makes a write safe to offer — the person sees it and can undo it in one click
# (30.6).  Since this card the brief goes into the **system prompt**, once, rather than in front
# of every prompt: a paragraph repeated per turn is the conversation, and a brief the model was
# told once is also what `session_info` can report.

#: The Board's brief is a file beside the policy (`board_chat_brief.md`) and is unchanged.
_FILE_BRIEFS = {"switchboard": "board_chat_brief.md"}

BRIEFS = {
    "projects": (
        "You are the agent in Relay's Projects tab, beside Sessions and Globals in the shared "
        "manager. Projects lists remembered projects and entry points for their boards, "
        "sessions, and terminals. Browsing a project does not attach the current terminal to it. "
        "Distinguish opening a new terminal in a project from attaching the current terminal, "
        "and change attachment only when requested. Use app_sessions_search for a project's "
        "past work and app_action_list to discover available navigation actions. Forgetting a "
        "project removes its remembered entry, not its files or live sessions."),
    "globals": (
        "You are the agent in Relay's Globals tab, the global Board HQ beside Projects "
        "and Sessions. It shows global memory cards, aliases, and original instruction files. "
        "Global cards live at RELAY_GLOBAL_SWITCHBOARD or XDG_CONFIG_HOME/relay/switchboard "
        "(normally ~/.config/relay/switchboard). Inspect the actual sources before answering. "
        "Project records of the same name override globals. Pinned memories and memories with "
        "workspace-matching paths are loaded at prompt refresh; retired, superseded, and team "
        "memories are inactive. Instructions remain in their original files and their existing "
        "Options selection controls loading. Change only what the user requested, preserve "
        "card identity and history, and retire obsolete cards instead of deleting them. The "
        "global board is not a fallback inbox for work unrelated to a project. "
        "User memory is what Relay knows about the person across projects. Use app_user_memory "
        "to list, inspect, save and retire those records; never file them on the current project board. "
        "When asked to interview the user, inspect saved user memories first, then ask ONE short "
        "question at a time, adapting to their answers and skipping what is already known. "
        "Start with their main work and where Relay can help. Cover preferred tools/languages, "
        "local versus SSH work, explanation depth, verification and collaboration preferences, "
        "long-running jobs, keyboard/voice/accessibility needs, and boundaries on what to remember. "
        "Offer skip, stop and correction at any time; do not require every topic. Ask only for "
        "information useful to Relay, never credentials, private identifiers or unrelated personal "
        "details. Do not infer traits from files, names or terminal history. After a few answers, "
        "show the exact concise facts you propose to save and ask which to remember. Explicit "
        "remember-this requests already authorize saving; do not reconfirm those. Save one fact "
        "per card with a stable name, scope: user, pinned: true and paths: []; update matching "
        "facts instead of duplicating them, preserve identity, and include source: user interview "
        "and a reviewed date. Read the latest hash before writing, report conflicts rather than "
        "overwriting, and say what was saved and how to review it in Globals > User memory. "
        "A preference about autonomy does not grant permission to take actions. Retire means "
        "excluded from future memory context, not deletion from source, history or old conversations."),
    "options": (
        "You are the helper agent in Relay's Options pane. It lists every setting Relay has, in "
        "sections, and the person is looking at it now. Read the rows with app_option_list and "
        "app_option_get rather than remembering what Relay's settings are, and answer about the "
        "values they actually have. app_option_set changes one for them and app_action_run runs "
        "one of Relay's own actions; both are shown to the person at once as \"Agent changed "
        "<row>: <before> → <after> · Undo\", so a change you make is never silent and is one "
        "click to take back. Change only what was asked for and say what you changed. API keys "
        "are secret: you cannot read or set them, so point at the row instead. When a setting "
        "is easier shown than described, app_open puts the pane on it."),
    "actions": (
        "You are the helper agent in Relay's Actions pane — the palette of everything Relay can "
        "do, with its keyboard shortcut beside it. app_action_list is that list; `agent_safe` "
        "says which ones you may run yourself (the ones the person can undo in a click) and the "
        "rest are for you to find and describe, with the shortcut, so they can run them. "
        "app_action_run runs one, and the person sees that it ran. When someone asks \"how do I "
        "…\", name the action and its shortcut, and app_open the palette at it. set_keybinding "
        "moves a shortcut — an action id from that list and the keys to put it on — and Relay "
        "reloads the file at once; rebind only what was asked for and say which keys the action "
        "is on afterwards."),
    # Owner, 2026-09-22: "there needs to be a helper agent on the model page".  The pane's four
    # tabs are the owner's steps of availability and then the jobs (src/ModelsPane.h); the
    # providers tab is Options › Models' own section, so its rows are the option tools' to read.
    "models": (
        "You are the helper agent in Relay's Models pane (Ctrl+Shift+M). It serves one terminal "
        "pane — the \"Serving:\" line on screen names it — and has four tabs, which are the steps "
        "a model goes through before a pane can run on it. providers: the providers this machine "
        "can reach and their API keys; it is Options › Models, so read its rows with "
        "app_option_list and app_option_get (section \"models\") and change one with "
        "app_option_set, which the person sees with an Undo. Keys are secret: you cannot read or "
        "set them, so name the row and let the person paste the key. available: every model the "
        "providers offer, with a tick for the ones that may appear in the lists and the model box. "
        "priorities: one section per class — high, main, flash, and local when this machine serves "
        "one — each an ordered list with a reasoning level per row and a cutoff for what shows in "
        "the box; the first available row of a class is what that class runs on. jobs: what each of "
        "Relay's background jobs (titles, summaries, compaction and the rest) runs on right now, "
        "which class it follows, and an override per job. app_panes says which model each pane is "
        "on. The available, priorities and jobs lists have no tool of their own: explain which "
        "row to tick, move or override and which key does it, and let the person do it. Answer "
        "from what the rows say now, not from what models you remember existing."),
    "sessions": (
        "You are the helper agent in Relay's Sessions pane — every past conversation and "
        "terminal session Relay has indexed. app_sessions_search is that index: it takes the "
        "pane's own query language (project:, file:, model:, branch:, before:, after:, is:, and "
        "-word to exclude) and answers from inside Relay, so nothing is sent anywhere. Search "
        "before you answer \"which session was that in\" — do not guess from memory — and "
        "app_open the pane at the search you used, so the person lands on the rows you are "
        "talking about.\n\n"
        # The owner, 2026-09-20: "sessions helper didn't do anything when I asked to open a group
        # of previous sessions in new panes", and then "can you make that more formalized that it
        # can do that?" — so opening one is a paragraph of its own, not a line in the tool schema.
        "You can also open a conversation, not only find it. app_open {target: \"conversation\", "
        "id} resumes one — exactly what pressing Enter on a row in this pane does — and "
        "{target: \"conversation\", ids: [\"…\", \"…\"]} opens a group, each in a pane of its "
        "own, in the order you list them; the result says what happened to each id. The ids come "
        "from app_sessions_search. new_pane decides where: when they said \"in new panes\" (or "
        "asked for several), it is true, which is the default; only when they said \"here\" or "
        "\"in this pane\" pass false, which loads it into the pane they are in and replaces what "
        "that pane was holding. If the ask does not say and it is one conversation, open it in a "
        "new pane and say so, or ask which they meant — never quietly take a pane over. When you "
        "list conversations in an answer, write each as a [title](session:<id>) link, so the row "
        "is one click away whether or not you opened it."),
}

#: The rule every console answers under (owner, 2026-09-20: "it also needs to reply in text that
#: it is doing it").  A console draws the agent's **text**; a turn that opens three panes and
#: says nothing reads as a turn that did nothing — which is how the report that started #FEJQ
#: began.  It goes after the context's own brief, in every context, because the app tools are in
#: every context.
SAY_WHAT_YOU_ARE_DOING = (
    "Say what you are doing, in text, whenever you act on the app — app_open, app_option_set, "
    "app_action_run, app_undo. One line before or alongside the call, naming the things: "
    "\"Opening 3 sessions in new panes: A, B, C.\", \"Turned Copy on select on — Undo is in the "
    "notification.\" Never finish a turn with an empty message after a tool call: the person sees "
    "your words, not your calls, and silence reads as nothing having happened.")


def brief_body(key: str) -> str:
    """The paragraph named by `brief.key`, or "" when the key names none.

    An unknown key is not an error: the GUI may name a context this worker is older than, and a
    console with no brief is a console that has to be told where it is by the person.
    """
    if not key:
        return ""
    if key in BRIEFS:
        return BRIEFS[key]
    name = _FILE_BRIEFS.get(key)
    if name is None:
        return ""
    import re
    path = Path(__file__).resolve().parent / name
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:                                        # pragma: no cover - packaging slip
        return ""
    return re.sub(r"<!--.*?-->", "", text, flags=re.S).strip()


def screen_line(screen) -> str:
    """The "On screen now:" line a turn's `screen` hint reaches the model as (30.7)."""
    text = " ".join(str(screen or "").split())[:MAX_SCREEN]
    return f"On screen now: {text}" if text else ""


# ---------------------------------------------------------------------------------------------
# Validation.  Every field is refused rather than coerced: a `configure` that cannot be believed
# is better refused at the pipe than answered with a pane's defaults in a console's clothes.

def _string(value, field: str, limit: int, *, required: bool = False) -> str:
    if value is None:
        if required:
            raise ValueError(f"context {field} is required.")
        return ""
    if not isinstance(value, str) or len(value) > limit:
        raise ValueError(f"context {field} must be text of at most {limit} characters.")
    return value.strip()


def validate_surface(value) -> str:
    """`ask {surface}` (33.2): which console asked, echoed on every event of that turn.

    Free text, because the GUI mints it and one worker may serve several consoles of one tab.
    Absent is "" — a worker with one console never has to send it.
    """
    if value is None or value == "":
        return ""
    if not isinstance(value, str) or len(value) > MAX_SURFACE or "\n" in value:
        raise ValueError(f"surface must be one line of at most {MAX_SURFACE} characters.")
    return value.strip()


def validate_screen(value) -> str:
    """`ask {screen}` (33.2): at most 2000 characters of what the asking surface is showing.

    **Not** `context`, which on `ask` is already the program/terminal context object
    (`queue.validate_context`); naming them the same is what made this field need a rename.
    """
    if value is None or value == "":
        return ""
    if not isinstance(value, str):
        raise ValueError("screen must be text saying what is on the asking surface.")
    return value[:MAX_SCREEN]


def default_scope(name: str) -> str:
    """The scope a context takes when `configure` names none.

    A terminal pane is a `pane` and everything else — a card's console included, since card
    #CTRN — is a `console`.  It is a default and not a rule — the GUI may name either — but it
    is the one that makes a `configure` from a GUI that knows about contexts and not about
    scopes do the right thing.
    """
    return "pane" if name == "terminal" else "console"


@dataclass
class ContextSpec:
    """The `context` block of `configure`, field for field with `relay::agent::ContextSpec`."""

    name: str = "terminal"
    agent_role: str = ""
    workspace: str = ""
    persist_scope: str = ""
    persist_key: str = ""
    brief_key: str = ""
    brief_title: str = ""
    brief_screen: str = ""
    scope: str = "pane"
    shell: bool = True
    routing: str = "auto"

    # ---- the wire ---------------------------------------------------------------
    @classmethod
    def from_json(cls, block) -> "ContextSpec | None":
        """Parse `configure {context}`.  None when the GUI sent none — every worker before this
        card did, and one that does behaves exactly as it did: a terminal pane."""
        if block is None:
            return None
        if not isinstance(block, dict):
            raise ValueError("configure context must be an object.")
        name = _string(block.get("name"), "name", MAX_NAME, required=True)
        if name not in NAMES:
            raise ValueError("context name must be one of " + ", ".join(NAMES) + ".")
        persist = block.get("persist") or {}
        if not isinstance(persist, dict):
            raise ValueError("context persist must be an object {scope, key}.")
        brief = block.get("brief") or {}
        if not isinstance(brief, dict):
            raise ValueError("context brief must be an object {key, title, screen}.")
        persist_scope = _string(persist.get("scope"), "persist.scope", MAX_NAME)
        if persist_scope not in PERSIST_SCOPES:
            raise ValueError("context persist.scope must be one of " +
                             ", ".join(s or '""' for s in PERSIST_SCOPES) + ".")
        persist_key = _string(persist.get("key"), "persist.key", MAX_KEY)
        if persist_scope and not persist_key:
            raise ValueError("context persist.scope was given without a persist.key.")
        scope = _string(block.get("scope"), "scope", MAX_NAME) or default_scope(name)
        scope = RETIRED_SCOPES.get(scope, scope)
        if scope not in SCOPES:
            raise ValueError("context scope must be one of " + ", ".join(SCOPES) + ".")
        routing = _string(block.get("routing"), "routing", MAX_NAME) or (
            "auto" if name == "terminal" else "agent")
        if routing not in ROUTINGS:
            raise ValueError("context routing must be one of " + ", ".join(ROUTINGS) + ".")
        shell = block.get("shell")
        if shell is None:
            shell = name == "terminal"
        if not isinstance(shell, bool):
            raise ValueError("context shell must be true or false.")
        return cls(name=name,
                   agent_role=_string(block.get("agent_role"), "agent_role", MAX_NAME),
                   workspace=_string(block.get("workspace"), "workspace", MAX_WORKSPACE),
                   persist_scope=persist_scope, persist_key=persist_key,
                   brief_key=_string(brief.get("key"), "brief.key", MAX_BRIEF_KEY),
                   brief_title=_string(brief.get("title"), "brief.title", MAX_TITLE),
                   brief_screen=validate_screen(brief.get("screen")),
                   scope=scope, shell=bool(shell), routing=routing)

    def to_json(self) -> dict:
        """The same bytes back, for `configured {context}` — what the GUI reads to confirm that
        the worker understood the surface it is drawn on."""
        return {"name": self.name, "agent_role": self.agent_role, "workspace": self.workspace,
                "persist": {"scope": self.persist_scope, "key": self.persist_key},
                "brief": {"key": self.brief_key, "title": self.brief_title,
                          "screen": self.brief_screen},
                "scope": self.scope, "shell": self.shell, "routing": self.routing}

    # ---- what it supplies ---------------------------------------------------------
    def is_console(self) -> bool:
        return self.scope == "console"

    def brief_text(self) -> str:
        """The brief as it goes into the system prompt: the title, the paragraph, the rule.

        Empty for a terminal pane, which is the context Relay's own `SYSTEM` prompt is written
        for — a second paragraph saying "you are in a terminal" would only cost tokens.
        """
        parts = []
        if self.brief_title:
            parts.append(f"[{self.brief_title}]")
        body = brief_body(self.brief_key)
        if body:
            parts.append(body)
        if self.scope == "console":
            parts.append(SAY_WHAT_YOU_ARE_DOING)
        line = screen_line(self.brief_screen)
        if line:
            parts.append(line)
        return "\n\n".join(parts).strip()

    def store(self, workspace: str) -> tuple[str | None, str | None]:
        """(session_dir, session_id) for this context's conversation, or (None, None).

        `helper` is the per-(project, tab) file of 30.7; the directory is derived from the
        workspace and the file name from the key, every time, so there is no index to fall out of
        step with the tabs and a tab that is gone leaves one small file behind rather than a
        dangling row.
        """
        if self.persist_scope == "helper" and self.persist_key:
            # Wherever it already is: see `helper_file`. A tab that gained a project keeps the
            # conversation it had, which is what "a restart brings each tab's helper back" means.
            directory, name = helper_file(workspace, self.persist_key)
            return str(directory), name
        return None, None

    def key(self) -> tuple[str, str, str]:
        """What makes this the *same* conversation as the last `configure`'s.

        A move of the workspace, the persistence key or the surface's name is a different
        console and its conversation is let go; a model swap or a keybinding reload moves none
        of them and leaves the conversation exactly where it was (30.7's rule, generalised).
        """
        return (self.name, self.workspace, f"{self.persist_scope}:{self.persist_key}")


def from_request(request: dict) -> "ContextSpec | None":
    """`configure {context}` → a spec, or None for a GUI that sends none."""
    return ContextSpec.from_json((request or {}).get("context"))
