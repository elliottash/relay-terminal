# SPDX-License-Identifier: AGPL-3.0-or-later
"""An agent's two read tools over the session it is in: `session_info` and `activity` (§30.5).

Info and Activity get no helper agent of their own (owner, 2026-09-20, card #FEJQ): both panes
are about the pane's **own** agent, and a second agent reading a ledger about the first is the
roundabout way to answer "why was that turn slow".  So the pane agent reads them itself.  That
decision is about whose helper sits on those two panes — not about whether an agent may ever
look at a pane that is not its own, which is what `pane` below is.

Everything here is already in the worker.  `session_info` is the `session_info` event of §25.3
for this pane's live session — the same call the ⓘ pane makes — and `activity` is a digest over
`Agent.turn_log`, the per-turn record protocol 11 already keeps for the tool-output and
transcript messages.  Nothing is collected twice and nothing is written.

The one rule that shapes the whole module: **the output is bounded, always.**  A tool whose
result grows with the conversation is exactly the tool that gets asked "why is my context low",
and answering must not be what lowers it.  So `session_info` carries the last
`MAX_INFO_HISTORY` turns of history with the true count beside it, `activity` defaults to the
last three turns, every list has a ceiling, and every string is cut.

Attached to every agent since #AGNT (`ActivityTools.attach` in `worker.py`): there is one
agent per worker, and a console reading "why was that turn slow" about a turn of its own is the
same question a pane asks.

**Another pane is read, never answered for** (#AG7R group 8, owner 2026-09-20: the card's last
open limitation was "the helper cannot read the pane it is helping").  Both tools take a `pane`,
which names any pane of the window the way `app_panes` gives it, and answer with what the
*window* knows about that pane — what it is titled, where it is, its model and input mode, and
whether its agent is busy.  Its turn record, its context and its token count stay with that
pane's own agent: they live in that pane's worker and nothing relays them, so reporting them
from here would mean inventing them.  That is the narrow answer on purpose — a pane's
conversation is the person's, and a helper in another pane has no claim on its words.

It is a **read**: the row comes from `app_panes`/`list_panes`, which the GUI answers whatever
"Agents may change options and run actions" says (§30.4 — listing panes is not a write), and
nothing on this path can change anything.  A pane that has closed since the list was read
answers `unknown_pane` with the ids that are live, never a raise.
"""
from __future__ import annotations

from typing import Callable

#: Turns of `history` the §25.3 payload keeps when a *tool* asks for it (§30.5).  The event the
#: ⓘ pane gets is unchanged; this cut is the tool's.
MAX_INFO_HISTORY = 20

#: `activity {turns}`: the default window and its ceiling (§30.5).
DEFAULT_TURNS = 3
MAX_TURNS = 20

#: `activity {slowest}`: how many slow calls may be listed.
MAX_SLOWEST = 20

#: Per-turn ceilings inside the digest.
MAX_TOOLS_PER_TURN = 12        # distinct tool names in the by-name roll-up
MAX_CALLS_IN_DETAIL = 40       # individual calls when one turn is asked for by id
MAX_PROMPT = 200               # the prompt's first line
MAX_PREVIEW = 120              # a call's own line

#: How many live pane ids an `unknown_pane` refusal names before it stops (#AG7R group 8).  The
#: refusal is read by a model that has just aimed at a pane that has gone, so it needs enough to
#: aim again and not a second listing: `app_panes` is that.
MAX_PANES_NAMED = 12


def _first_line(text, limit: int = MAX_PROMPT) -> str:
    """A prompt's first line, cut: the digest says which turn it was, not what was in it."""
    if not isinstance(text, str):
        return ""
    line = next((part.strip() for part in text.splitlines() if part.strip()), "")
    return line if len(line) <= limit else line[:limit - 1] + "…"


def _call_line(entry: dict) -> str:
    """The one line a call is known by: its §23 label when it has one, else its preview's tail."""
    label = entry.get("label")
    if isinstance(label, dict) and isinstance(label.get("title"), str) and label["title"].strip():
        line = label["title"].strip()
        if isinstance(label.get("path"), str) and label["path"]:
            line = f"{line} {label['path']}"
        return _first_line(line, MAX_PREVIEW)
    # Previews start with a title line ("RUN COMMAND"); the last line is the command or path,
    # which is what `turn_summary` shows too.
    preview = str(entry.get("preview") or "").strip()
    return _first_line(preview.splitlines()[-1], MAX_PREVIEW) if preview else ""


def _outcome(entry: dict) -> str:
    if entry.get("ok"):
        return "ok"
    result = entry.get("result")
    if isinstance(result, dict) and isinstance(result.get("error"), str):
        return _first_line(result["error"], 120) or "error"
    if isinstance(entry.get("exit_code"), int):
        return f"exit {entry['exit_code']}"
    return "error"


class ActivityToolError(ValueError):
    """A refusal the model should read and correct, in `board_tools.BoardToolError`'s shape."""

    def __init__(self, message: str, code: str = "failed"):
        super().__init__(message)
        self.code = code

    def to_result(self) -> dict:
        return {"error": str(self), "code": self.code}


def spec(name: str, description: str, properties: dict, required: list[str]) -> dict:
    return {"type": "function", "function": {"name": name, "description": description,
            "parameters": {"type": "object", "properties": properties, "required": required,
                           "additionalProperties": False}}}


TOOL_SPECS = [
    spec("session_info",
         "What this pane's ⓘ view shows about the conversation you are in: the model and "
         "provider, the workspace and git branch, how much of the context window is used, the "
         "token usage and cost so far, the instructions loaded, the subagent threads, and the "
         "recent turns. Use it to answer questions about your own session instead of guessing. "
         "Name a `pane` to ask about another pane of the window instead: you get what the window "
         "knows about it — its title, directory, model, input mode and whether it is busy — and "
         "not its conversation, which stays with that pane's own agent.",
         {"pane": {"type": "string",
                   "description": "Another pane of this window, as app_panes gives it. Omit it "
                                  "for the conversation you are in."}}, []),
    spec("activity",
         "A digest of your own recent turns — what the Activity pane draws: for each turn its "
         "first line, the model, how long it took, the tokens it used, the tools it called with "
         "their times, and how it ended. Use it to answer \"why was that slow\" or \"what did "
         "you just do\". It is always a window, never the whole session. Name a `pane` to ask "
         "about another pane of the window instead: you get what it is doing and whether it is "
         "busy, not its turns, which stay with that pane's own agent.",
         {"turns": {"type": "integer", "minimum": 1, "maximum": MAX_TURNS,
                    "description": f"How many of the most recent turns to digest (default {DEFAULT_TURNS}, at most {MAX_TURNS})."},
          "turn": {"type": "string",
                   "description": "One turn id, from a previous activity call: that turn's calls one by one."},
          "slowest": {"type": "integer", "minimum": 1, "maximum": MAX_SLOWEST,
                      "description": f"Instead of a window, the N slowest tool calls of the session (at most {MAX_SLOWEST})."},
          "pane": {"type": "string",
                   "description": "Another pane of this window, as app_panes gives it. Omit it "
                                  "for your own turns."}},
         []),
]

TOOL_NAMES = tuple(s["function"]["name"] for s in TOOL_SPECS)


class ActivityTools:
    """`session_info` and `activity` for one agent (§30.5).

    Holds the agent itself — it is the agent's own record it reads — and, when the worker has
    one, the callable that builds the §25.3 payload (`session_protocol.SessionCommands.live_info`,
    which is what the ⓘ pane's own message answers with, so the tool and the pane can never
    disagree).  Without it, `session_info` falls back to what the agent alone can say.

    A `pane` goes through the worker's own `AppTools` (`agent.app`), because `app_panes` is the
    one place a pane id exists and a second listing would go stale against it (#AG7R group 2).
    Read through the agent rather than held here, so a `configure` that replaces the app tools
    replaces what these two read as well.
    """

    def __init__(self, agent, live_info: Callable[[], dict] | None = None):
        self.agent = agent
        self.live_info = live_info

    @classmethod
    def attach(cls, agent, live_info: Callable[[], dict] | None = None) -> "ActivityTools":
        """Give this agent the two read tools and fold their note into its system prompt."""
        tools = cls(agent, live_info)
        agent.activity = tools
        agent.refresh_system_prompt()
        return tools

    # ---- dispatch, the BoardTools shape ----------------------------------------
    def tool_specs(self) -> list[dict]:
        return [dict(s) for s in TOOL_SPECS]

    def handles(self, name: str) -> bool:
        return name in TOOL_NAMES

    def preview(self, name: str, args: dict) -> str:
        keys = ("pane",) if name == "session_info" else ("turns", "turn", "slowest", "pane")
        bits = [f"{key}: {args[key]}" for key in keys
                if isinstance(args, dict) and args.get(key) is not None]
        if name == "session_info":
            return "SESSION INFO" + ("\n\n" + "\n".join(bits) if bits else "")
        return "ACTIVITY\n\n" + ("\n".join(bits) or "the last few turns")

    def run(self, name: str, args: dict) -> dict:
        if not self.handles(name):
            raise ActivityToolError(f"unknown tool {name!r}")
        if not isinstance(args, dict):
            raise ActivityToolError("Tool arguments must be an object.")
        args = dict(args)
        try:
            # `pane` first, on both tools: a call that names somebody else's pane must never be
            # answered out of this agent's own record, which is what asking about a pane got
            # before #AG7R group 8 (the model had no way to say which pane it meant, so it read
            # its own and reported it as theirs).
            other = self._aimed_pane(args.pop("pane", None))
            if other is not None:
                return self._other_pane(other, name)
            return self._session_info() if name == "session_info" else self._activity(args)
        except ActivityToolError as exc:
            return exc.to_result()
        except (ValueError, OSError) as exc:
            return {"error": str(exc)[:500], "code": "failed"}

    # ---- another pane of the window (#AG7R group 8) -----------------------------
    def _aimed_pane(self, pane) -> dict | None:
        """The row of the pane a call names, or None when it means the session it is in.

        None for a pane `app_panes` marked `you`: a pane agent naming its own token means "me",
        and answering that with the outside view would be a worse answer than the one it asked
        for.  The helper of a tab has no pane of its own, so nothing it names is ever `you`.
        """
        if pane is None:
            return None
        if not isinstance(pane, str) or not pane.strip():
            raise ActivityToolError("pane must be a pane id from app_panes.", code="invalid_value")
        pane = pane.strip()
        rows = self._pane_rows()
        row = next((r for r in rows if r.get("id") == pane), None)
        if row is None:
            live = ", ".join(str(r.get("id")) for r in rows[:MAX_PANES_NAMED]) or "none"
            raise ActivityToolError(
                f"Relay has no pane {pane!r} in this window any more — it has been closed, or "
                f"the id is from an older list. The panes there now are: {live}. "
                "app_panes lists them with their titles.", code="unknown_pane")
        return None if row.get("you") else row

    def _pane_rows(self) -> list[dict]:
        """The window's panes, from `app_panes` — never a second listing of our own.

        A read whatever the writes toggle says: `list_panes` changes nothing and the GUI answers
        it with "Agents may change options and run actions" off (§30.4), so a `pane` on either
        tool is refused only where the app tools do not exist at all.
        """
        app = getattr(self.agent, "app", None)
        if app is None:
            raise ActivityToolError(
                "This worker cannot see Relay's panes, so `pane` names nothing here. Ask about "
                "the session you are in instead.", code="failed")
        result = app.run("app_panes", {})
        if not isinstance(result, dict):
            raise ActivityToolError("Relay did not list its panes.")
        if result.get("error"):
            raise ActivityToolError(str(result["error"]), code=str(result.get("code") or "failed"))
        rows = result.get("panes")
        return [row for row in rows if isinstance(row, dict)] if isinstance(rows, list) else []

    @staticmethod
    def _other_pane(row: dict, name: str) -> dict:
        """What the window knows about a pane that is not this agent's, and what it does not.

        Deliberately the narrow answer (#AG7R group 8).  The turn record, the context left and
        the token count are held in that pane's own worker and nothing relays them here, so the
        alternative to saying so is guessing; and a pane's conversation is the person's, not
        something a helper on the next pane is owed.  `turns_readable` says it in a field as
        well as in the note, so a model does not have to parse prose to learn it.
        """
        title = str(row.get("title") or row.get("id") or "that pane")
        bits = [f'"{title}" is ' + ("busy" if row.get("busy") else "idle")]
        if row.get("model"):
            bits.append(f"on {row['model']}")
        if row.get("mode"):
            bits.append(f"in {row['mode']} mode")
        if row.get("cwd"):
            bits.append(f"at {row['cwd']}")
        mine = "its conversation" if name == "session_info" else "its turns"
        return {"pane": dict(row), "you": False, "busy": bool(row.get("busy")),
                "turns_readable": False, "text": ", ".join(bits) + ".",
                "note": f"That is what the window knows about the pane, not {mine}: a pane's "
                        "turn record, context and token count are its own agent's and are not "
                        "readable from another pane. Say what you can see and, if they need the "
                        "rest, tell them to ask in that pane."}

    # ---- session_info ----------------------------------------------------------
    def _session_info(self) -> dict:
        """The §25.3 `session_info` payload for the live session, with `history` windowed.

        `turns` stays the true count: the cut is in what is *listed*, never in what is reported,
        so "how many turns have we had" is still answered exactly.
        """
        if self.live_info is None:
            raise ActivityToolError(
                "This pane cannot read its own session record here.", code="failed")
        event = dict(self.live_info())
        event.pop("event", None)
        event.pop("id", None)
        history = event.get("history")
        if isinstance(history, list):
            event["history"] = history[-MAX_INFO_HISTORY:]
            if len(history) > MAX_INFO_HISTORY:
                event["history_truncated"] = len(history) - MAX_INFO_HISTORY
                event["note"] = (f"The last {MAX_INFO_HISTORY} of {len(history)} turns are listed; "
                                 "`turns` is the true count. Use `activity` for the turns.")
        return event

    # ---- activity --------------------------------------------------------------
    def _records(self) -> list[dict]:
        """The turn log, oldest first.  `Agent` keeps the last `MAX_TURN_LOG` of them."""
        lock = getattr(self.agent, "_lock", None)
        log = getattr(self.agent, "turn_log", None) or {}
        if lock is not None:
            with lock:
                return list(log.values())
        return list(log.values())               # pragma: no cover - a stub agent in a test

    def _activity(self, args: dict) -> dict:
        for key in ("turns", "slowest"):
            if args.get(key) is not None and type(args[key]) is not int:
                raise ActivityToolError(f"{key} must be an integer.", code="invalid_value")
        if args.get("turn") is not None and not isinstance(args["turn"], str):
            raise ActivityToolError("turn must be a turn id.", code="invalid_value")
        records = self._records()
        if not records:
            return {"turns": [], "count": 0, "session_turns": 0,
                    "note": "This conversation has had no turns in this worker yet."}
        if args.get("turn"):
            return self._one_turn(records, args["turn"].strip())
        if args.get("slowest") is not None:
            return self._slowest(records, max(1, min(int(args["slowest"]), MAX_SLOWEST)))
        window = max(1, min(int(args.get("turns") or DEFAULT_TURNS), MAX_TURNS))
        chosen = records[-window:]
        return {"turns": [self._digest(record) for record in chosen], "count": len(chosen),
                "session_turns": len(records),
                "note": (f"The last {len(chosen)} of {len(records)} turns this worker holds"
                         + (f" (the agent keeps at most {len(records)})." if len(records) > len(chosen)
                            else ".")
                         + " Ask for one by id for its calls, or `slowest` for the slow ones.")}

    def _digest(self, record: dict) -> dict:
        """One turn, rolled up: never the calls themselves, only what they were and how long."""
        entries = list(record.get("tools", {}).values())
        by_name: dict[str, dict] = {}
        for entry in entries:
            row = by_name.setdefault(entry.get("name") or "?",
                                     {"tool": entry.get("name") or "?", "calls": 0, "ms": 0,
                                      "failed": 0})
            row["calls"] += 1
            row["ms"] += int(entry.get("ms") or 0)
            if not entry.get("ok"):
                row["failed"] += 1
        tools = sorted(by_name.values(), key=lambda r: (-r["ms"], -r["calls"]))
        out = {"turn_id": record.get("turn_id"), "request": _first_line(record.get("prompt")),
               "outcome": record.get("outcome") or "running",
               "running": record.get("elapsed_ms") is None,
               "ms": record.get("elapsed_ms"), "thinking_ms": record.get("thinking_ms"),
               "tool_calls": len(entries), "tools": tools[:MAX_TOOLS_PER_TURN]}
        if isinstance(record.get("model"), str) and record["model"]:
            out["model"] = record["model"]
        usage = record.get("usage")
        if isinstance(usage, dict) and any(usage.values()):
            out["tokens"] = {k: v for k, v in usage.items() if v}
        if len(tools) > MAX_TOOLS_PER_TURN:
            out["tools_omitted"] = len(tools) - MAX_TOOLS_PER_TURN
        if record.get("stop_reason"):
            out["stop_reason"] = record["stop_reason"]
        return out

    def _one_turn(self, records: list[dict], turn_id: str) -> dict:
        record = next((r for r in records if r.get("turn_id") == turn_id), None)
        if record is None:
            known = ", ".join(str(r.get("turn_id")) for r in records[-5:])
            raise ActivityToolError(
                f"No turn {turn_id!r} in this worker's log (it keeps the most recent turns). "
                f"The latest are: {known}.", code="unknown_turn")
        entries = list(record.get("tools", {}).values())
        calls = [{"call_id": entry.get("call_id"), "tool": entry.get("name"),
                  "ms": entry.get("ms"), "outcome": _outcome(entry),
                  "what": _call_line(entry)}
                 for entry in entries[:MAX_CALLS_IN_DETAIL]]
        out = {**self._digest(record), "calls": calls}
        if len(entries) > MAX_CALLS_IN_DETAIL:
            out["calls_omitted"] = len(entries) - MAX_CALLS_IN_DETAIL
        return out

    def _slowest(self, records: list[dict], count: int) -> dict:
        rows = []
        for record in records:
            for entry in record.get("tools", {}).values():
                rows.append({"turn_id": record.get("turn_id"), "call_id": entry.get("call_id"),
                             "tool": entry.get("name"), "ms": int(entry.get("ms") or 0),
                             "outcome": _outcome(entry), "what": _call_line(entry)})
        rows.sort(key=lambda r: -r["ms"])
        total = len(rows)
        return {"slowest": rows[:count], "count": min(count, total), "tool_calls": total,
                "turns_searched": len(records),
                "note": f"The {min(count, total)} slowest of {total} tool calls in the "
                        f"{len(records)} turns this worker still holds."}


def prompt_section(tools: "ActivityTools | None") -> str:
    """What `Agent.system_prompt` appends when this agent can read the session it is in (§30.5)."""
    if tools is None:
        return ""
    line = ("\nYour own session: session_info answers questions about this conversation (model, "
            "context left, tokens, workspace, instructions) and activity digests your recent "
            "turns and their tool calls — use them for \"why was that slow\", \"what did you just "
            "do\" and \"how much context is left\" rather than guessing or re-reading the "
            "conversation.")
    # The panes around this one (#AG7R group 8). Only where the app tools exist: without them
    # there is no `app_panes` and no pane id, so naming one would be advice that cannot be taken.
    if getattr(tools.agent, "app", None) is not None:
        line += (" Another pane of the window is not yours to read this way: app_panes says "
                 "which panes there are and whether each is busy, and `pane` on either tool says "
                 "the same about one of them — its turns, context and tokens stay with its own "
                 "agent, so say what you can see and let them ask in that pane for the rest.")
    return line + "\n"
