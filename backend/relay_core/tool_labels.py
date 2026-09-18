# SPDX-License-Identifier: GPL-3.0-or-later
"""One concise line per tool call, and the detail behind it.

Owner, 2026-09-18 (card #TK9C): "agent tool calls are too detailed. rather than seeing a mini
python script, i would rather see something like 'executed python' … you can then click on them to
uncollapse the full details. similarly with 'wrote x.py' or 'edited x.py' … concise and informative
and allow easy access of relevant information."

Everything here is a pure function of the tool name, its arguments and its result: no I/O, no clock,
no workspace. The backend calls `started_label` before a call runs, `result_label` when it is done
and `detail` when a surface asks for the stored output (`tool_output_get`). What the fields mean is
the contract in `docs/AGENT-SESSIONS-PROTOCOL.md` section 23; every renderer reads them and none of
them parses `preview`, which stays on the wire for surfaces that have not been updated.
"""
from __future__ import annotations

import json
import re
import shlex

#: A diff of at most this many changed lines (added + removed) is printed under the line without a
#: click; a bigger one opens the diff pane instead.
INLINE_DIFF_LINES = 12
#: A single-line command no longer than this is shown whole; a longer one shrinks to its program.
SHORT_COMMAND = 40
#: First line of an error, capped.
ERROR_CAP = 120
#: Per-string cap for the arguments kept for the fold view. Mirrors nothing in tools.py: it only
#: has to be big enough for a command line (16 KiB there) and small enough to keep 50 turns cheap.
ARG_TEXT_CAP = 16384
#: Caps for `detail` sections, matching what tools.py already stored (MAX_OUTPUT, MAX_FILE).
DETAIL_OUTPUT_CAP = 32768
DETAIL_TEXT_CAP = 131072

#: Every kind a label may carry. view/web/external are reserved for tools Relay does not have yet
#: (a screenshot/preview tool, a web fetch or search, an out-of-process MCP tool); `other` is the
#: catch-all a tool nobody has taught this module gets.
KINDS = ("run", "job", "read", "list", "edit", "agent", "plan", "skill", "board", "config",
         "input", "view", "web", "external", "other")
#: Styles a `detail` section may ask for.
DETAIL_STYLES = ("code", "output", "diff", "args", "error", "text")

MINUS = "−"  # the real minus sign, for "+3 −1"

# ----------------------------------------------------------------- the command classifier

_ASSIGNMENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*=")
_DURATION = re.compile(r"^\d+(?:\.\d+)?[smhd]?$")
_SECRET_WORD = re.compile(r"(?i)key|token|secret|password|passwd|credential|cookie|auth|bearer")

#: Wrapper → how many of its own non-flag arguments to skip before the real command.
_WRAPPERS = {"sudo": 0, "doas": 0, "time": 0, "command": 0, "exec": 0, "nohup": 0, "setsid": 0,
             "stdbuf": 0, "nice": 0, "ionice": 0, "env": 0, "timeout": 1, "flock": 1,
             "xvfb-run": 0, "dbus-run-session": 0}
#: Wrapper flags that take a value of their own.
_FLAG_WITH_VALUE = {"-u", "-g", "-n", "-c", "-s", "-k", "-C", "--user", "--group", "--signal",
                    "--kill-after", "--server-args", "--auto-servernum"}
#: Programs whose first non-flag word is the interesting half of the name ("git commit").
_SUBCOMMAND_CLIS = {"git", "npm", "pnpm", "yarn", "bun", "cargo", "docker", "podman", "kubectl",
                    "go", "pip", "pip3", "uv", "poetry", "apt", "apt-get", "dnf", "yum", "brew",
                    "systemctl", "journalctl", "gh", "glab", "make", "just", "rustup", "conda",
                    "flatpak", "snap", "terraform", "helm", "aws", "gcloud", "az"}
#: Programs whose inline script (-c/-e/heredoc) is "a script", not a command line.
_INTERPRETERS = {"python", "python2", "python3", "bash", "sh", "zsh", "dash", "ksh", "fish",
                 "node", "deno", "perl", "ruby", "php", "lua", "Rscript"}
_SCRIPT_FLAGS = {"-c", "-e", "--eval", "--command", "-"}


def command_label(command) -> str:
    """The text after "ran " for one shell command. Never raises, whatever it is handed."""
    try:
        return _command_label(command)
    except Exception:  # a label is never worth an exception in the turn loop
        try:
            return str(command).strip().split()[0][:SHORT_COMMAND] or "command"
        except Exception:
            return "command"


def _command_label(command) -> str:
    if not isinstance(command, str) or not command.strip():
        return "command"
    text = command.strip()
    if "\n" not in text and len(text) <= SHORT_COMMAND and not _looks_secret(text):
        return text
    head, heredoc = _before_heredoc(text)
    segments = [s for s in (seg.strip() for seg in _split_segments(head)) if s]
    if not segments:
        return _first_word(text)
    # "cd build && make" is one command, not two: leading cd segments are scaffolding.
    while len(segments) > 1 and _first_word(segments[0]) == "cd":
        segments.pop(0)
    name = _segment_label(segments[0], heredoc)
    extra = len(segments) - 1
    return f"{name} +{extra}" if extra > 0 else name


def _segment_label(segment: str, heredoc: bool) -> str:
    program, rest = _program_of(segment)
    if not program:
        return _first_word(segment)
    base = _basename(program)
    if base in _INTERPRETERS:
        if heredoc or any(token in _SCRIPT_FLAGS for token in rest[:3]):
            return f"{base} script"
        if "-m" in rest:
            index = rest.index("-m")
            if index + 1 < len(rest):
                return _shorter(f"{base} -m {rest[index + 1]}", base)
        for token in rest:
            if token.startswith("-"):
                continue
            return _shorter(f"{base} {_basename(token)}", base)
        return base
    if base in _SUBCOMMAND_CLIS:
        sub = _subcommand(rest)
        return _shorter(f"{base} {sub}", base) if sub else base
    return base


def _shorter(candidate: str, fallback: str) -> str:
    """A two-word name only while it stays short and says nothing it should not."""
    if len(candidate) > SHORT_COMMAND or _looks_secret(candidate):
        return fallback
    return candidate


def _subcommand(tokens: list[str]) -> str:
    skip = 0
    for index, token in enumerate(tokens):
        if skip:
            skip -= 1
            continue
        if token == "--":
            continue
        if token.startswith("-"):
            if token in _FLAG_WITH_VALUE and "=" not in token:
                skip = 1
            continue
        if _ASSIGNMENT.match(token):
            continue
        return token if not _looks_secret(token) else ""
    return ""


def _program_of(segment: str) -> tuple[str, list[str]]:
    """The real program of one segment and the tokens after it, past env and wrappers."""
    tokens = _tokens(segment)
    index, guard = 0, 0
    while index < len(tokens) and guard < 64:
        guard += 1
        token = tokens[index]
        if not token or token in {"!", "{", "}", "(", ")", "&&", "||", "|", ";"}:
            index += 1
            continue
        if _ASSIGNMENT.match(token):
            index += 1
            continue
        base = _basename(token)
        if base in _WRAPPERS:
            index += 1
            skip_value = 0
            while index < len(tokens) and (tokens[index].startswith("-") or skip_value):
                if skip_value:
                    skip_value = 0
                    index += 1
                    continue
                flag = tokens[index]
                index += 1
                if flag in _FLAG_WITH_VALUE and "=" not in flag:
                    skip_value = 1
            own = _WRAPPERS[base]
            while own and index < len(tokens):
                if base == "timeout" and not _DURATION.match(tokens[index]):
                    break
                index += 1
                own -= 1
            continue
        return token, tokens[index + 1:]
    return ("", []) if not tokens else (tokens[0], tokens[1:])


def _tokens(segment: str) -> list[str]:
    try:
        parsed = shlex.split(segment, comments=False, posix=True)
    except ValueError:  # unbalanced quotes: the model wrote something odd, still label it
        parsed = segment.split()
    return [t.strip("()") for t in parsed if t.strip("()") or t]


def _basename(token: str) -> str:
    name = token.strip().strip("'\"").rstrip("/")
    name = name.rpartition("/")[2] or name
    return name


def _first_word(text: str) -> str:
    for word in text.split():
        cleaned = _basename(word)
        if cleaned and not _ASSIGNMENT.match(cleaned):
            return cleaned[:SHORT_COMMAND]
    return "command"


def _before_heredoc(text: str) -> tuple[str, bool]:
    """Everything up to a heredoc's `<<`, and whether there was one: a heredoc body is data."""
    index = text.find("<<")
    if index < 0:
        return text, False
    return text[:index], True


def _split_segments(text: str) -> list[str]:
    """Top-level `|`, `||`, `&&`, `;`, `&` and newline splits, respecting quotes and escapes."""
    parts: list[str] = []
    current: list[str] = []
    quote = None
    index, length = 0, len(text)
    while index < length:
        char = text[index]
        if quote:
            current.append(char)
            if char == "\\" and quote == '"' and index + 1 < length:
                current.append(text[index + 1])
                index += 2
                continue
            if char == quote:
                quote = None
            index += 1
            continue
        if char in "'\"":
            quote = char
            current.append(char)
            index += 1
            continue
        if char == "\\" and index + 1 < length:
            current.append(char)
            current.append(text[index + 1])
            index += 2
            continue
        if char in "|&;\n":
            parts.append("".join(current))
            current = []
            index += 2 if text[index:index + 2] in ("||", "&&") else 1
            continue
        current.append(char)
        index += 1
    parts.append("".join(current))
    return parts


def _looks_secret(text: str) -> bool:
    """Whether a command line carries something that reads like a credential."""
    for token in str(text).split():
        head = re.split(r"[=:]", token, maxsplit=1)[0]
        if head != token and _SECRET_WORD.search(head):
            return True
        if token.startswith("-") and _SECRET_WORD.search(token):
            return True
    return False


# ----------------------------------------------------------------- labels

def started_label(name, args, *, existed=None) -> dict:
    """What to show while the call runs: kind, running text, the title as far as it is known."""
    base = _base(name, _dict(args), existed)
    label = {"kind": base["kind"], "running": base["running"], "title": base["title"]}
    if base.get("path"):
        label["path"] = base["path"]
    if base.get("merge"):
        label["merge"] = dict(base["merge"])
    return label


def result_label(name, args, result, *, ms=None, existed=None) -> dict:
    """The finished line: title, stats, ok/error, what a click opens, and how it may merge."""
    args, result = _dict(args), _dict(result)
    created = result.get("created")
    if name == "write_file" and isinstance(created, bool):
        existed = not created
    base = _base(name, args, existed)
    ok = call_ok(result)
    # A command that ran and exited 1 still "ran"; only a call that never happened (an error or a
    # refusal) falls back to the plain verb, which is what the ✗ line shows.
    happened = ok or not _error_message(result)
    label = {"kind": base["kind"], "running": base["running"],
             "title": base["title"] if happened else base["failed"], "ok": ok}
    if result.get("still_running"):
        label["kind"] = "job"
    if base.get("path"):
        label["path"] = base["path"]
    stats = _stats(name, args, result, base, ms=ms, ok=ok)
    if stats:
        label["stats"] = stats
    message = _error_message(result)
    if message:
        label["error"] = message
    changed = _changed_lines(result)
    if changed is not None and ok:
        label["inline_diff"] = changed <= INLINE_DIFF_LINES
    label["open"] = _open(name, args, result, base, ok, changed)
    merge = base.get("merge")
    if merge and ok:
        label["merge"] = _merge_counts(name, dict(merge), result)
    return label


def call_ok(result) -> bool:
    """The same verdict agent._record_tool records, plus the `ok: false` tools answer with."""
    if not isinstance(result, dict):
        return False
    if "error" in result or result.get("ok") is False:
        return False
    if result.get("timed_out"):
        return False
    return result.get("exit_code") in (None, 0)


def error_text(result) -> str:
    """The first line of an error, capped — what the ✗ line says after the title."""
    return _error_message(_dict(result)) or "failed"


def _error_message(result) -> str:
    """The message of a call that did not happen. A nonzero exit code is not one of those: the
    command ran, and `stats` already says "exit 1"."""
    result = _dict(result)
    value = result.get("error")
    if isinstance(value, str) and value.strip():
        return value.strip().splitlines()[0].strip()[:ERROR_CAP]
    if result.get("ok") is False:
        refused = result.get("refused")
        return f"refused: {refused}"[:ERROR_CAP] if isinstance(refused, str) else "refused"
    if result.get("timed_out"):
        return "timed out"
    return ""


def _base(name, args: dict, existed) -> dict:
    """kind, present tense, past tense, the failed form, and the fixed half of merge/path."""
    if name == "run_command":
        command = command_label(args.get("command"))
        if args.get("background"):
            return _row("job", f"starting {command}", f"started job: {command}",
                        f"start job: {command}")
        return _row("run", f"running {command}", f"ran {command}", f"run {command}")
    if name == "command_output":
        job = _short(args.get("job_id"), 24) or "job"
        return _row("job", f"reading {job}", f"read {job} output", f"read {job} output")
    if name == "stop_command":
        job = _short(args.get("job_id"), 24) or "job"
        return _row("job", f"stopping {job}", f"stopped {job}", f"stop {job}")
    if name == "read_file":
        path = _path(args)
        return _row("read", f"reading {_file_name(path)}", f"read {_file_name(path)}",
                    f"read {_file_name(path)}", path=path,
                    merge={"key": "read", "singular": "file", "plural": "files"})
    if name == "list_directory":
        path = _path(args)
        shown = _dir_name(path)
        return _row("list", f"listing {shown}", f"listed {shown}", f"list {shown}", path=path,
                    merge={"key": "list", "singular": "folder", "plural": "folders"})
    if name in ("write_file", "edit_file"):
        path = _path(args)
        shown = _file_name(path)
        edit = name == "edit_file" or existed is True
        return _row("edit", f"{'editing' if edit else 'writing'} {shown}",
                    f"{'edited' if edit else 'wrote'} {shown}",
                    f"{'edit' if edit else 'write'} {shown}", path=path)
    if name == "update_todos":
        return _row("plan", "updating todos", "updated todos", "update todos")
    if name == "write_plan":
        title = _short(args.get("title"), 40)
        return _row("plan", "writing the plan", f"wrote plan “{title}”" if title else "wrote the plan",
                    "write the plan")
    if name == "agent":
        what = _short(args.get("description"), 40) or _short(args.get("subagent_type"), 30) or "subagent"
        return _row("agent", f"starting subagent “{what}”", f"started subagent “{what}”",
                    f"start subagent “{what}”")
    if name == "agent_message":
        who = _short(args.get("id"), 24) or "subagent"
        return _row("agent", f"messaging {who}", f"messaged {who}", f"message {who}")
    if name == "agent_wait":
        who = _short(args.get("id"), 24) or "the background subagents"
        return _row("agent", f"waiting for {who}", f"waited for {who}", f"wait for {who}")
    if name == "type_into_program":
        typed = _typed(args)
        return _row("input", f"typing {typed}", f"typed {typed}", "type into the program")
    if name == "run_in_terminal":
        command = command_label(args.get("command"))
        if args.get("mode") == "prefill":
            return _row("run", f"prefilling {command}", f"prefilled {command}", f"prefill {command}")
        return _row("run", f"running {command} in your terminal",
                    f"ran {command} in your terminal", f"run {command} in your terminal")
    if name == "set_keybinding":
        action = _short(args.get("action"), 40) or "a shortcut"
        keys = ", ".join(k for k in args.get("keys") or [] if isinstance(k, str))[:40]
        done = f"bound {action} to {keys}" if keys else f"unbound {action}"
        return _row("config", f"binding {action}", done, f"bind {action}")
    if name == "load_skill":
        skill = _short(args.get("name"), 40) or "a skill"
        return _row("skill", f"loading skill {skill}", f"loaded skill {skill}", f"load skill {skill}")
    if name == "read_skill_file":
        skill, path = _short(args.get("name"), 30), _short(args.get("path"), 60)
        shown = f"{skill}/{path}" if skill and path else (path or skill or "a skill file")
        return _row("skill", f"reading {shown}", f"read {shown}", f"read {shown}",
                    merge={"key": "read", "singular": "file", "plural": "files"})
    if isinstance(name, str) and name.startswith("board_"):
        return _board(name, args)
    return _fallback(name, args)


def _board(name: str, args: dict) -> dict:
    card = _card_id(args.get("id") or args.get("into"))
    what = name[len("board_"):].replace("_", " ")
    if name == "board_list":
        return _row("board", "searching the board", "searched the board", "search the board")
    if name == "board_read":
        return _row("board", f"reading card {card}", f"read card {card}", f"read card {card}",
                    card=card)
    if name == "board_create_card":
        title = _short(args.get("title"), 40)
        return _row("board", "creating a card", f"created card “{title}”" if title else "created a card",
                    "create a card")
    if name == "board_update_card":
        return _row("board", f"updating card {card}", f"updated card {card}", f"update card {card}",
                    card=card)
    if name == "board_move_card":
        status = _short(args.get("status"), 30) or _short(args.get("tab"), 30)
        done = f"moved card {card} → {status}" if status else f"moved card {card}"
        return _row("board", f"moving card {card}", done, f"move card {card}", card=card)
    if name == "board_comment":
        kind = _short(args.get("kind"), 20) or "note"
        return _row("board", f"commenting on card {card}", f"commented on card {card} · {kind}",
                    f"comment on card {card}", card=card)
    if name == "board_merge_cards":
        count = len(args.get("cards") or []) if isinstance(args.get("cards"), list) else 0
        return _row("board", f"merging {count or 'the'} cards into {card}",
                    f"merged {count} card{'' if count == 1 else 's'} into {card}",
                    f"merge cards into {card}", card=card)
    if name == "board_split_card":
        parts = len(args.get("parts") or []) if isinstance(args.get("parts"), list) else 0
        return _row("board", f"splitting card {card}", f"split card {card} into {parts}",
                    f"split card {card}", card=card)
    if name == "board_sections":
        return _row("board", "changing the board's sections", "changed the board's sections",
                    "change the board's sections")
    return _row("board", f"{what} on the board", f"{what} on the board", f"{what} on the board",
                card=card)


def _fallback(name, args: dict) -> dict:
    """A tool nobody has taught this module: its humanised name and its first short argument."""
    human = _humanise(name)
    hint = _first_short_arg(args)
    done = f"{human} {hint}" if hint else human
    kind = "external" if isinstance(name, str) and "__" in name else "other"
    return _row(kind, f"running {human}", done, done)


def _row(kind: str, running: str, title: str, failed: str, *, path=None, merge=None,
         card=None) -> dict:
    row = {"kind": kind, "running": running, "title": title, "failed": failed}
    if path:
        row["path"] = path
    if merge:
        row["merge"] = merge
    if card:
        row["card"] = card
    return row


# ----------------------------------------------------------------- stats, open, merge

def _stats(name, args: dict, result: dict, base: dict, *, ms, ok: bool) -> list[str]:
    stats: list[str] = []
    kind = base["kind"]
    if kind in ("run", "job") and name != "run_in_terminal":
        lines = _count_lines(result.get("output"))
        if lines:
            stats.append(_plural(lines, "line"))
    elif kind == "read":
        lines = _count_lines(result.get("content"))
        if lines:
            stats.append(_plural(lines, "line"))
    elif kind == "list":
        entries = result.get("entries")
        if isinstance(entries, list):
            stats.append(_plural(len(entries), "entry", "entries"))
    elif kind == "edit":
        added, removed = result.get("added"), result.get("removed")
        if result.get("created"):
            # A new file has no "before": its size, not a diff of the whole thing.
            stats.append("new")
            if isinstance(added, int) and added:
                stats.append(_plural(added, "line"))
        elif isinstance(added, int) and isinstance(removed, int):
            stats.append(f"+{added} {MINUS}{removed}" if added or removed else "no change")
        replacements = result.get("replacements")
        if isinstance(replacements, int) and replacements > 1:
            stats.append(f"{replacements} replacements")
    elif kind == "skill":
        lines = _count_lines(result.get("content"))
        if lines:
            stats.append(_plural(lines, "line"))
        files = result.get("files")
        if isinstance(files, list) and files:
            stats.append(_plural(len(files), "file"))
    elif name == "update_todos":
        if isinstance(result.get("open"), int):
            stats.append(f"{result['open']} open")
    elif name == "agent":
        if isinstance(result.get("tools"), int) and result["tools"]:
            stats.append(_plural(result["tools"], "tool"))
        if result.get("background"):
            stats.append("in the background")
    elif name == "board_list":
        cards = result.get("cards")
        if isinstance(cards, list):
            stats.append(_plural(len(cards), "card"))
    elif name == "run_in_terminal" and result.get("downgraded"):
        stats.append("prefilled instead")
    if result.get("still_running"):
        job = result.get("job_id")
        stats.append(f"still running as {job}" if isinstance(job, str) else "still running")
    if result.get("stopped"):
        stats.append("stopped")
    exit_code = result.get("exit_code")
    if isinstance(exit_code, int) and (exit_code != 0 or kind in ("run", "job")):
        stats.append(f"exit {exit_code}")
    if result.get("timed_out"):
        stats.append("timed out")
    if result.get("truncated"):
        stats.append("truncated")
    duration = _duration(result, ms)
    if duration:
        stats.append(duration)
    return stats


def _open(name, args: dict, result: dict, base: dict, ok: bool, changed) -> dict:
    """What a click does. The default everywhere is folding the detail open in place."""
    fold = {"type": "fold"}
    if not ok:
        return fold
    if base["kind"] == "edit":
        if result.get("created") and base.get("path"):
            return {"type": "file", "path": base["path"]}
        if changed is not None and changed > INLINE_DIFF_LINES:
            return {"type": "diff"}
        return fold
    if name == "read_file" and base.get("path"):
        return {"type": "file", "path": base["path"]}
    if name == "agent" and isinstance(result.get("id"), str):
        return {"type": "subagent", "id": result["id"]}
    if name in ("agent_message", "agent_wait") and isinstance(args.get("id"), str):
        return {"type": "subagent", "id": args["id"]}
    if name == "write_plan":
        return {"type": "plan"}
    if name == "update_todos":
        return {"type": "todos"}
    if base.get("card") or isinstance(result.get("id"), str) and str(name).startswith("board_"):
        card = base.get("card") or _card_id(result.get("id"))
        if card:
            return {"type": "card", "id": card.lstrip("#")}
    return fold


def _merge_counts(name, merge: dict, result: dict) -> dict:
    if merge["key"] == "read":
        merge["lines"] = _count_lines(result.get("content"))
    else:
        entries = result.get("entries")
        merge["entries"] = len(entries) if isinstance(entries, list) else 0
    return merge


def _changed_lines(result: dict):
    added, removed = result.get("added"), result.get("removed")
    if isinstance(added, int) and isinstance(removed, int):
        return added + removed
    return None


def _duration(result: dict, ms) -> str:
    seconds = result.get("duration_seconds")
    if not isinstance(seconds, (int, float)) or isinstance(seconds, bool):
        seconds = (ms / 1000.0) if isinstance(ms, (int, float)) and not isinstance(ms, bool) else None
    if seconds is None or seconds < 1:
        return ""
    if seconds >= 10:
        return f"{round(seconds)} s"
    text = f"{seconds:.1f}"
    return f"{text[:-2] if text.endswith('.0') else text} s"


# ----------------------------------------------------------------- the fold view

def detail(name, args, result, *, preview: str = "", diff=None) -> list[dict]:
    """Ordered sections for the fold: what was asked, what came back, the error if any."""
    args, result = _dict(args), _dict(result)
    base = _base(name, args, None)
    kind, sections = base["kind"], []
    if kind in ("run", "job"):
        command = args.get("command")
        if isinstance(command, str) and command.strip():
            sections.append(_section("command", "code", command))
        cwd = args.get("cwd")
        if isinstance(cwd, str) and cwd not in ("", "."):
            sections.append(_section("working directory", "text", cwd))
        if isinstance(args.get("intent"), str):
            sections.append(_section("intent", "text", args["intent"]))
        sections.append(_section("output", "output", result.get("output") or "",
                                 cap=DETAIL_OUTPUT_CAP, keep_empty=True,
                                 truncated=bool(result.get("truncated"))))
    elif kind == "edit":
        text = diff if isinstance(diff, str) and diff.strip() else _diff_from_preview(preview)
        sections.append(_section("diff", "diff", text or "(no text changes)", cap=DETAIL_TEXT_CAP,
                                 keep_empty=True))
    elif kind == "read" or (kind == "skill" and name == "read_skill_file"):
        sections.append(_section("contents", "output", result.get("content") or "",
                                 cap=DETAIL_TEXT_CAP, keep_empty=True,
                                 truncated=bool(result.get("truncated"))))
    elif kind == "list":
        entries = result.get("entries")
        listing = "\n".join(
            f"{e.get('name')}{'/' if e.get('type') == 'directory' else ''}"
            for e in entries if isinstance(e, dict)) if isinstance(entries, list) else ""
        sections.append(_section("entries", "output", listing, cap=DETAIL_OUTPUT_CAP,
                                 keep_empty=True, truncated=bool(result.get("truncated"))))
    elif kind == "skill":
        sections.append(_section("skill", "output", result.get("content") or "",
                                 cap=DETAIL_TEXT_CAP, keep_empty=True,
                                 truncated=bool(result.get("truncated"))))
    elif name == "write_plan":
        sections.append(_section("plan", "text", args.get("content") or "", cap=DETAIL_TEXT_CAP,
                                 keep_empty=True))
    elif name == "update_todos":
        items = args.get("items") if isinstance(args.get("items"), list) else []
        listing = "\n".join(f"[{i.get('status')}] {i.get('text')}" for i in items if isinstance(i, dict))
        sections.append(_section("todos", "args", listing, keep_empty=True))
    elif name == "agent":
        if isinstance(args.get("prompt"), str):
            sections.append(_section("task", "text", args["prompt"], cap=DETAIL_TEXT_CAP))
        report = (result.get("result") or {}).get("text") if isinstance(result.get("result"), dict) \
            else result.get("result")
        if isinstance(report, str) and report.strip():
            sections.append(_section("report", "text", report, cap=DETAIL_TEXT_CAP))
    elif name == "type_into_program":
        if isinstance(args.get("intent"), str):
            sections.append(_section("intent", "text", args["intent"]))
        sections.append(_section("keystroke", "code", _typed(args), keep_empty=True))
        if isinstance(result.get("screen"), str) and result["screen"]:
            sections.append(_section("screen", "output", result["screen"], cap=DETAIL_OUTPUT_CAP))
    else:
        rendered = _args_text(args)
        if rendered:
            sections.append(_section("arguments", "args", rendered))
        changes = result.get("changes")
        if isinstance(changes, list) and changes:
            sections.append(_section("changes", "args", "\n".join(str(c) for c in changes)))
    if not call_ok(result):
        message = result.get("error")
        sections.append(_section("error", "error",
                                 message if isinstance(message, str) else error_text(result),
                                 keep_empty=True))
    return [s for s in sections if s]


def _section(heading: str, style: str, text, *, cap: int = ARG_TEXT_CAP, keep_empty: bool = False,
             truncated: bool = False) -> dict | None:
    body = text if isinstance(text, str) else ""
    if not body and not keep_empty:
        return None
    cut = len(body) > cap
    section = {"heading": heading, "style": style, "text": body[:cap]}
    if cut or truncated:
        section["truncated"] = True
    return section


def _args_text(args: dict) -> str:
    lines = []
    for key, value in args.items():
        if isinstance(value, str):
            text = value
        else:
            try:
                text = json.dumps(value, ensure_ascii=False)
            except (TypeError, ValueError):
                text = str(value)
        lines.append(f"{key}: {text[:ARG_TEXT_CAP]}")
    return "\n".join(lines)


def _diff_from_preview(preview) -> str:
    """The diff out of a write's legacy preview, for a call recorded before `diff` was carried.

    The one place in Relay that still reads `preview` as data, and only as a fallback.
    """
    if not isinstance(preview, str) or not preview:
        return ""
    lines = preview.splitlines(keepends=True)
    start = next((i for i, line in enumerate(lines) if line.startswith("--- ")), None)
    if start is None:
        return ""
    end = len(lines)
    for index in range(len(lines) - 1, start, -1):
        if lines[index].startswith("Old bytes: "):
            end = index
            break
    return "".join(lines[start:end]).strip("\n")


def safe_args(name, args) -> dict:
    """The arguments worth keeping for the fold: strings capped, everything else compacted.

    type_into_program keeps only what its own preview already shows the user (the intent, the
    keystroke); nothing else about what was typed is ever copied anywhere by this module.
    """
    args = _dict(args)
    out = {}
    for key, value in args.items():
        if isinstance(value, str):
            out[key] = value[:ARG_TEXT_CAP]
        elif isinstance(value, (int, float, bool)) or value is None:
            out[key] = value
        else:
            try:
                out[key] = json.loads(json.dumps(value, ensure_ascii=False)[:ARG_TEXT_CAP])
            except (TypeError, ValueError):
                out[key] = str(value)[:ARG_TEXT_CAP]
    return out


# ----------------------------------------------------------------- small helpers

def _dict(value) -> dict:
    return value if isinstance(value, dict) else {}


def _path(args: dict) -> str:
    path = args.get("path")
    return path.strip() if isinstance(path, str) and path.strip() else ""


def _file_name(path: str) -> str:
    if not path:
        return "a file"
    return path if len(path) <= SHORT_COMMAND else _basename(path)


def _dir_name(path: str) -> str:
    if not path or path == ".":
        return "./"
    shown = path if len(path) <= SHORT_COMMAND else _basename(path)
    return shown.rstrip("/") + "/"


def _typed(args: dict) -> str:
    key = args.get("key")
    if isinstance(key, str) and key:
        return f"<{_short(key, 20)}>"
    text = args.get("text")
    if isinstance(text, str) and text:
        shown = _short(text.replace("\n", "⏎"), SHORT_COMMAND)
        return f"“{shown}”"
    return "nothing"


def _card_id(value) -> str:
    if not isinstance(value, str) or not value.strip():
        return ""
    return "#" + value.strip().lstrip("#")[:8]


def _humanise(name) -> str:
    text = re.sub(r"[_\-]+", " ", str(name or "tool")).strip()
    return " ".join(text.split())[:60] or "tool"


def _first_short_arg(args: dict) -> str:
    for value in args.values():
        if isinstance(value, str) and value.strip() and "\n" not in value and len(value) <= 60 \
                and not _looks_secret(value):
            return value.strip()
    return ""


def _short(value, limit: int) -> str:
    if not isinstance(value, str):
        return ""
    text = " ".join(value.split())
    return text[:limit - 1] + "…" if len(text) > limit else text


def _count_lines(text) -> int:
    if not isinstance(text, str) or not text:
        return 0
    return text.count("\n") + (0 if text.endswith("\n") else 1)


def _plural(count: int, singular: str, plural: str | None = None) -> str:
    word = singular if count == 1 else (plural or singular + "s")
    return f"{count:,} {word}"
