# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tool groups a turn loads when it needs them, instead of carrying on every request.

#GMCF decision 9 (2026-09-20), from
`docs/qa_evidence/2026-09-20-perf-fixes/prompt-distillation/PROPOSAL.md` section 2.7 and decision 9.
Three groups — Relay's own Options and actions, this pane's session, and the project's tests — are
8.6 KB of schemas (~2,150 tokens) that a minority of turns use: "open that setting", "why was that
slow", "run the tests the card names". They are named in one line of the prompt and their schemas
arrive when the model asks for them with `load_tools`, appended to the **end** of the tool list from
the next request on, so everything the provider has already cached is still byte-identical.

This is Claude Code's own deferred-tool shape, copied deliberately (the owner's standing rule: if
the reference design already does it, copy its shape): the names are known, the schemas are not,
one tool fetches them, and calling a name whose schema has not been fetched is refused with a
sentence saying how to fetch it.

**Never on the Local tier.** There the chat template renders the tools *above* the system prompt, so
appending one schema re-prefills the whole request — 13 s measured (proposal 4.1). The short profile
does not offer these groups at all (decision 7), and deferral is off for any local endpoint, so a
load can only ever happen where a provider caches by prefix.
"""
from __future__ import annotations

#: group id -> (tool names, what the group is for, in the model's terms).
GROUPS: dict[str, tuple[tuple[str, ...], str]] = {
    "app": (("app_option_list", "app_option_get", "app_option_set", "app_action_list",
             "app_action_run", "app_panes", "app_send_prompt", "app_prefill_prompt", "app_rename",
             "app_sessions_search", "app_open", "app_changes", "app_undo"),
            "read or change Relay's own Options, run one of its actions, list the window's panes "
            "so an action can be aimed at one, send or pre-fill a prompt in another pane, name a "
            "pane or a tab, search the user's past conversations, or put a screen in front of "
            "them"),
    "own_session": (("session_info", "activity"),
                    "read this conversation itself: its model, context left, tokens, and a digest "
                    "of recent turns with their tool-call timings"),
    "tests": (("tests_check", "tests_run"),
              "check what a Switchboard card's `## Tests` section names, and run named tests"),
}

#: Which group a tool belongs to, or None.
_OWNER = {name: group for group, (names, _what) in GROUPS.items() for name in names}

LOAD_TOOLS = "load_tools"
LOAD_TOOLS_SPEC = {"type": "function", "function": {
    "name": LOAD_TOOLS,
    "description": ("Load a group of tools whose names you know and whose schemas you do not yet "
                    "have. The schemas arrive in the next request and stay for the rest of the "
                    "conversation; calling one of the names before loading its group is refused. "
                    "Load a group when the request needs it, not in advance."),
    "parameters": {"type": "object", "properties": {
        "group": {"type": "string", "enum": sorted(GROUPS),
                  "description": "The group to load."}},
        "required": ["group"], "additionalProperties": False}}}


def group_of(name: str) -> str | None:
    return _OWNER.get(name)


def deferred_names(groups) -> tuple[str, ...]:
    """Every tool name still held back, for the assembler to filter the list with."""
    return tuple(name for group in groups for name in GROUPS[group][0])


def prompt_line(groups) -> str:
    """The one-line rule: the names that exist, and how to get their schemas.

    One line and not a paragraph on purpose — it is on every request of every turn, which is what
    the groups themselves were costing. It names the tools rather than describing them: a name plus
    the group's own sentence is what the model needs to decide whether to ask for the schema.
    """
    ordered = [g for g in GROUPS if g in set(groups)]
    if not ordered:
        return ""
    parts = [f"{group} ({', '.join(GROUPS[group][0])}) to {GROUPS[group][1]}" for group in ordered]
    return ("More tools you can load with load_tools when a request needs them, by group: "
            + "; ".join(parts) + ".")


def refusal(name: str, group: str) -> str:
    """What a call to a name whose schema has not been fetched gets back."""
    return (f"{name} is not loaded in this conversation. Call {LOAD_TOOLS} with "
            f'group="{group}" first; its schemas are in the next request, and then you can call '
            f"{name}.")


def validate(arguments) -> str:
    if not isinstance(arguments, dict) or set(arguments) - {"group"}:
        raise ValueError(f"{LOAD_TOOLS} takes one argument, group.")
    group = arguments.get("group")
    if group not in GROUPS:
        raise ValueError(f"group must be one of {', '.join(sorted(GROUPS))}.")
    return group


def result(group: str, already: bool) -> dict:
    """What the tool returns: the names now callable, and where their schemas are."""
    names = list(GROUPS[group][0])
    return {"loaded": group, "tools": names, "already_loaded": already,
            "note": ("Their schemas are in the tool list of the next request — call one of them "
                     "then, not in this response." if not already else
                     "This group was already loaded; its schemas are in the tool list.")}
