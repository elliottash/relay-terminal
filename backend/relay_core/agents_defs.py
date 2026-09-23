# SPDX-License-Identifier: AGPL-3.0-or-later
"""Subagent definitions from every tool's agent directories, mapped to one common model.

Formats (verified 2026-09-17 against each project's docs or source):

* Relay ``.relay/agents``, Claude Code ``.claude/agents``: Markdown with YAML frontmatter
  (``name, description, tools, disallowedTools, model, effort, maxTurns, background``); body = prompt.
* opencode ``.opencode/{agent,agents}/**/*.md``: name from the relative path unless frontmatter sets it;
  ``description, mode (subagent|primary|all), model (provider/model), variant, steps|maxSteps,
  tools {name: bool} (deprecated), permission {edit|bash|...: allow|ask|deny}, disable``.
* Codex ``.codex/agents/*.toml``: ``name, description, developer_instructions, model,
  model_reasoning_effort, sandbox_mode``.
* Gemini CLI ``.gemini/agents/*.md``: ``name, description, kind, tools [gemini names], model,
  max_turns``.
* Cursor ``.cursor/agents/*.md``: ``name, description, model, readonly, is_background``.

Directories are read in precedence order; the first definition of a name wins and later ones are
reported as duplicates. Built-ins come last, so a user file named ``general`` replaces the built-in.
The YAML reader is a small subset parser (the worker runs without site-packages).
"""
from __future__ import annotations

import os
import re
from dataclasses import dataclass, field
from pathlib import Path

try:  # Python 3.11+
    import tomllib
except ImportError:  # pragma: no cover
    tomllib = None

MAX_FILE_BYTES = 64 * 1024
MAX_PROMPT_BYTES = 16 * 1024
MAX_DEFINITIONS = 200
MAX_FILES_SCANNED = 1000
MAX_DEPTH = 4
MAX_STEPS = 50
EFFORTS = ("low", "medium", "high", "max")
NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._/-]{0,99}$")

# Tools a subagent may ever receive. Never agent tools (no nesting) or set_keybinding.
SUBAGENT_TOOLS = ("run_command", "read_file", "list_directory", "write_file", "edit_file", "load_skill", "read_skill_file")
READ_ONLY_TOOLS = ("run_command", "read_file", "list_directory", "load_skill", "read_skill_file")

# Lower-cased tool names from Claude Code, opencode, Gemini CLI and Relay itself.
TOOL_MAP: dict[str, tuple[str, ...]] = {
    # Relay
    **{name: (name,) for name in SUBAGENT_TOOLS},
    # Claude Code / opencode
    "read": ("read_file",), "bash": ("run_command",), "grep": ("run_command",),
    "glob": ("list_directory",), "ls": ("list_directory",), "list": ("list_directory",),
    # A string-replacing edit tool is Relay's edit_file; a name that also creates or rewrites whole
    # files (opencode's patch, Gemini's replace with an empty old_string) grants write_file too.
    "write": ("write_file",), "edit": ("edit_file",), "multiedit": ("edit_file",),
    "notebookedit": ("write_file",), "patch": ("edit_file", "write_file"),
    "skill": ("load_skill", "read_skill_file"),
    # Gemini CLI
    "run_shell_command": ("run_command",), "grep_search": ("run_command",), "search_file_content": ("run_command",),
    "read_many_files": ("read_file",), "replace": ("edit_file", "write_file"), "activate_skill": ("load_skill", "read_skill_file"),
}
# Names that grant the full shell although the source tool treats them as read-only searches.
SHELL_WIDENING = {"grep", "grep_search", "search_file_content"}

# Model aliases from other tools. Values are Relay preset ids or "inherit"; configure may override.
DEFAULT_ALIASES = {"haiku": "inherit", "sonnet": "inherit", "opus": "inherit", "flash": "inherit"}


@dataclass
class AgentDefinition:
    name: str
    description: str
    prompt: str = ""
    tools: tuple[str, ...] = SUBAGENT_TOOLS
    model: str = "inherit"
    effort: str | None = None
    max_steps: int = 12
    background: bool | None = None
    read_only: bool = False
    source: str = "builtin"
    tool: str = "relay"          # which tool's format it came from
    warnings: list[str] = field(default_factory=list)

    def to_dict(self) -> dict:
        return {"name": self.name, "description": self.description, "source": self.source,
                "format": self.tool, "model": self.model, "effort": self.effort,
                "tools": list(self.tools), "max_steps": self.max_steps, "background": self.background,
                "read_only": self.read_only, "warnings": list(self.warnings)}


BUILTINS = [
    AgentDefinition(
        "general",
        "General-purpose agent with the same file and command tools as the main agent. Use for "
        "investigation or implementation. Put task-specific constraints (such as do not edit) in the prompt.",
        "You are a general-purpose agent. Complete the task, verify what you did, and finish with a "
        "concise report of what changed and what was verified.",
        SUBAGENT_TOOLS, "inherit", None, 12),
    # Signal threads (#AQ6X decision 9): the unasked pickup of a failing check nobody is on.  Its
    # own definition rather than `general` for two reasons a user can see.  The thread's row in the
    # Sessions manager is told apart by `agent_type`, so a pickup is listed and marked without a
    # schema change; and the prompt can say the one thing that makes this run different from every
    # other subagent — nobody asked for it, nobody is watching it, and the *check* decides whether
    # it worked.  The task text (`relay_core.signal_threads.task_text`) carries the fault itself.
    AgentDefinition(
        "signal",
        "Works one machine-reported fault — a failing test, a broken build — that no pane claimed. "
        "Relay starts these itself; they are not for the main agent to spawn.",
        "You are working one fault that a machine found and nobody claimed. "
        "Nobody asked for this run and nobody is watching it: do not ask questions, and do nothing "
        "the fault does not need. "
        "The check decides whether you succeeded, not your report: run it until it passes, or say "
        "out loud that you could not fix it. "
        "Never claim a test passes unless a successful run of it proves so. "
        "Stay inside the fault: a second problem you notice is not yours to fix in this run.",
        SUBAGENT_TOOLS, "inherit", None, 12),
]


# ----- locations ----------------------------------------------------------------
def default_locations(workspace: str | Path, home: str | Path | None = None) -> list[tuple[Path, str]]:
    ws = Path(workspace)
    home = Path(home) if home is not None else Path.home()
    return [
        (ws / ".relay/agents", "relay"), (home / ".config/relay/agents", "relay"),
        (ws / ".claude/agents", "claude"), (home / ".claude/agents", "claude"),
        (ws / ".opencode/agent", "opencode"), (ws / ".opencode/agents", "opencode"),
        (home / ".config/opencode/agent", "opencode"), (home / ".config/opencode/agents", "opencode"),
        (ws / ".codex/agents", "codex"), (home / ".codex/agents", "codex"),
        (ws / ".gemini/agents", "gemini"), (home / ".gemini/agents", "gemini"),
        (ws / ".cursor/agents", "cursor"), (home / ".cursor/agents", "cursor"),
    ]


def guess_format(directory: Path) -> str:
    parts = set(directory.parts)
    for marker, tool in ((".claude", "claude"), (".codex", "codex"), (".gemini", "gemini"),
                         (".cursor", "cursor"), (".opencode", "opencode"), ("opencode", "opencode")):
        if marker in parts:
            return tool
    return "relay"


# ----- YAML subset --------------------------------------------------------------
class DefinitionError(ValueError):
    pass


_KEY = re.compile(r"""^("(?:[^"\\]|\\.)*"|'(?:[^']|'')*'|[^\s:#'"\-\[{][^:]*?|-[^\s:][^:]*?)\s*:(?:\s+(.*)|\s*)$""")


def _indent(line: str) -> int:
    return len(line) - len(line.lstrip(" "))


def _meaningful(line: str) -> bool:
    stripped = line.strip()
    return bool(stripped) and not stripped.startswith("#")


def _strip_comment(value: str) -> str:
    quote = None
    for i, ch in enumerate(value):
        if quote:
            if ch == quote:
                quote = None
        elif ch in "\"'" and (i == 0 or value[i - 1] in " [{,:"):
            quote = ch
        elif ch == "#" and (i == 0 or value[i - 1] in " \t"):
            return value[:i].rstrip()
    return value.strip()


def _split_flow(inner: str) -> list[str]:
    parts, depth, quote, current = [], 0, None, ""
    for ch in inner:
        if quote:
            current += ch
            if ch == quote:
                quote = None
            continue
        if ch in "\"'":
            quote = ch
        elif ch in "[{":
            depth += 1
        elif ch in "]}":
            depth -= 1
        elif ch == "," and depth == 0:
            parts.append(current.strip())
            current = ""
            continue
        current += ch
    if current.strip():
        parts.append(current.strip())
    return parts


def _scalar(raw: str):
    value = _strip_comment(raw)
    if value.startswith("[") and value.endswith("]"):
        return [_scalar(part) for part in _split_flow(value[1:-1])]
    if value.startswith("{") and value.endswith("}"):
        out = {}
        for part in _split_flow(value[1:-1]):
            match = _KEY.match(part)
            if match:
                out[_unquote_key(match.group(1))] = _scalar(match.group(2) or "")
        return out
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        inner = value[1:-1]
        if value[0] == '"':
            return inner.replace('\\"', '"').replace("\\n", "\n").replace("\\\\", "\\")
        return inner.replace("''", "'")
    lowered = value.lower()
    if lowered in ("true", "yes", "on"):
        return True
    if lowered in ("false", "no", "off"):
        return False
    if lowered in ("null", "~", ""):
        return None
    if re.fullmatch(r"-?\d+", value):
        return int(value)
    if re.fullmatch(r"-?\d+\.\d+", value):
        return float(value)
    return value


def _unquote_key(key: str) -> str:
    key = key.strip()
    if len(key) >= 2 and key[0] == key[-1] and key[0] in "\"'":
        return key[1:-1]
    return key


def _block_scalar(lines, i, parent_indent, style):
    block = []
    while i < len(lines) and (not lines[i].strip() or _indent(lines[i]) > parent_indent):
        block.append(lines[i])
        i += 1
    while block and not block[-1].strip():
        block.pop()
    base = min((_indent(line) for line in block if line.strip()), default=0)
    texts = [line[base:] if line.strip() else "" for line in block]
    if style.startswith(">"):
        paragraphs, current = [], []
        for text in texts:
            if text:
                current.append(text.strip())
            else:
                paragraphs.append(" ".join(current))
                current = []
        paragraphs.append(" ".join(current))
        return "\n".join(p for p in paragraphs if p).strip(), i
    return "\n".join(texts).strip("\n"), i


def _parse_node(lines, i, min_indent):
    while i < len(lines) and not _meaningful(lines[i]):
        i += 1
    if i >= len(lines) or _indent(lines[i]) < min_indent:
        return None, i
    indent = _indent(lines[i])
    if lines[i].strip() == "-" or lines[i].strip().startswith("- "):
        return _parse_list(lines, i, indent)
    return _parse_map(lines, i, indent)


def _parse_list(lines, i, indent):
    items = []
    while i < len(lines):
        if not _meaningful(lines[i]):
            i += 1
            continue
        stripped = lines[i].strip()
        if _indent(lines[i]) != indent or not (stripped == "-" or stripped.startswith("- ")):
            break
        rest = stripped[1:].strip()
        i += 1
        if rest:
            items.append(_scalar(rest))
        else:
            value, i = _parse_node(lines, i, indent + 1)
            items.append(value)
    return items, i


def _parse_map(lines, i, indent):
    result: dict = {}
    while i < len(lines):
        if not _meaningful(lines[i]):
            i += 1
            continue
        current = _indent(lines[i])
        if current < indent:
            break
        stripped = lines[i].strip()
        match = _KEY.match(stripped) if current == indent else None
        if not match:
            i += 1
            continue
        key, rest = _unquote_key(match.group(1)), (match.group(2) or "").strip()
        i += 1
        if rest in (">", "|", ">-", "|-", ">+", "|+"):
            result[key], i = _block_scalar(lines, i, indent, rest)
        elif rest == "" or rest.startswith("#"):
            j = i
            while j < len(lines) and not _meaningful(lines[j]):
                j += 1
            if j < len(lines) and _indent(lines[j]) == indent and lines[j].strip().startswith("- "):
                result[key], i = _parse_list(lines, j, indent)
            else:
                result[key], i = _parse_node(lines, i, indent + 1)
        else:
            result[key] = _scalar(rest)
    return result, i


def parse_yaml(text: str) -> dict:
    value, _ = _parse_node(text.splitlines(), 0, 0)
    return value if isinstance(value, dict) else {}


def split_frontmatter(text: str) -> tuple[dict, str]:
    lines = text.splitlines()
    if not lines or lines[0].strip() != "---":
        raise DefinitionError("missing YAML frontmatter")
    for end in range(1, len(lines)):
        if lines[end].strip() in ("---", "..."):
            return parse_yaml("\n".join(lines[1:end])), "\n".join(lines[end + 1:]).strip()
    raise DefinitionError("unterminated YAML frontmatter")


# ----- mapping --------------------------------------------------------------------
def _names(value) -> list[str]:
    if isinstance(value, str):
        return [part.strip() for part in value.split(",") if part.strip()]
    if isinstance(value, list):
        return [str(part).strip() for part in value if part is not None and str(part).strip()]
    return []


def map_tool_names(names, warnings: list[str]) -> list[str]:
    out: list[str] = []
    for raw in names:
        if raw == "*":
            out.extend(SUBAGENT_TOOLS)
            continue
        if "(" in raw:
            warnings.append(f"tool {raw!r}: argument patterns cannot be enforced by Relay; tool not granted")
            continue
        key = raw.lower()
        mapped = TOOL_MAP.get(key)
        if mapped is None:
            warnings.append(f"tool {raw!r} has no Relay equivalent; ignored")
            continue
        if key in SHELL_WIDENING:
            warnings.append(f"tool {raw!r} mapped to run_command (full shell, not read-only)")
        out.extend(mapped)
    return out


def _ordered(tools) -> tuple[str, ...]:
    chosen = set(tools)
    return tuple(name for name in SUBAGENT_TOOLS if name in chosen)


def _opencode_permissions(tools: set[str], permission, warnings: list[str]) -> set[str]:
    # A permission short of "allow" withholds the tool, so each file-changing key withholds both
    # write_file and edit_file: opencode's edit/write/patch permissions all gate changing a file.
    keys = {"edit": ("write_file", "edit_file"), "write": ("write_file", "edit_file"),
            "patch": ("write_file", "edit_file"),
            "bash": ("run_command",), "read": ("read_file",), "list": ("list_directory",)}
    if isinstance(permission, str):
        permission = {key: permission for key in ("edit", "bash")}
    if not isinstance(permission, dict):
        return tools
    for key, action in permission.items():
        targets = keys.get(str(key).lower())
        if targets is None:
            continue
        if isinstance(action, dict):
            values = {str(v).lower() for v in action.values()}
            allowed = values == {"allow"}
            if not allowed:
                warnings.append(f"permission {key}: per-pattern rules cannot be enforced; tool not granted")
        else:
            allowed = str(action).lower() == "allow"
            if str(action).lower() == "ask":
                warnings.append(f"permission {key}: 'ask' has no Relay approval step; tool not granted")
        if not allowed:
            tools -= set(targets)
    return tools


def _effort(value, warnings: list[str]) -> str | None:
    if value is None or value == "":
        return None
    text = str(value).lower()
    text = {"minimal": "low", "xhigh": "max"}.get(text, text)
    if text in EFFORTS:
        return text
    warnings.append(f"effort {value!r} is not one of {', '.join(EFFORTS)}; ignored")
    return None


def _steps(value, default, warnings: list[str]) -> int:
    if value is None:
        return default
    if isinstance(value, int) and not isinstance(value, bool) and value > 0:
        return min(value, MAX_STEPS)
    warnings.append(f"step limit {value!r} is not a positive integer; using {default}")
    return default


def _model(value) -> str:
    text = str(value).strip() if value not in (None, "") else "inherit"
    return text or "inherit"


def definition_from_fields(fields: dict, body: str, *, name: str, source: str, tool: str) -> AgentDefinition | None:
    """Map one file's fields to the common model. Returns None for files that are not subagents."""
    warnings: list[str] = []
    name = str(fields.get("name") or name).strip()
    if not NAME.match(name):
        raise DefinitionError(f"unsupported agent name {name!r}")
    if fields.get("disable") is True:
        return None
    mode = fields.get("mode")
    if tool == "opencode" and mode == "primary":
        return None
    if fields.get("kind") not in (None, "local"):
        raise DefinitionError(f"agent kind {fields.get('kind')!r} is not supported")
    description = " ".join(str(fields.get("description") or "").split())
    if not description:
        raise DefinitionError("no description")
    prompt = body if tool != "codex" else str(fields.get("developer_instructions") or "")
    if tool == "codex" and not prompt.strip():
        raise DefinitionError("no developer_instructions")
    if len(prompt.encode("utf-8")) > MAX_PROMPT_BYTES:
        prompt = prompt.encode("utf-8")[:MAX_PROMPT_BYTES].decode("utf-8", "ignore")
        warnings.append(f"prompt truncated to {MAX_PROMPT_BYTES} bytes")

    tools: set[str] = set(SUBAGENT_TOOLS)
    raw_tools = fields.get("tools")
    if isinstance(raw_tools, dict):  # opencode {name: bool}
        for key, enabled in raw_tools.items():
            mapped = map_tool_names([str(key)], warnings)
            if enabled is False:
                tools -= set(mapped)
    elif raw_tools is not None:
        tools = set(map_tool_names(_names(raw_tools), warnings))
    disallowed = fields.get("disallowedTools", fields.get("disallowed_tools"))
    if disallowed is not None:
        tools -= set(map_tool_names(_names(disallowed), warnings))
    if tool == "opencode":
        tools = _opencode_permissions(tools, fields.get("permission"), warnings)

    read_only = fields.get("readonly") is True or fields.get("sandbox_mode") == "read-only"
    if read_only:
        tools -= {"write_file", "edit_file"}

    background = fields.get("background", fields.get("is_background"))
    effort = _effort(fields.get("effort", fields.get("model_reasoning_effort", fields.get("variant"))), warnings)
    steps = _steps(fields.get("maxTurns", fields.get("max_turns", fields.get("steps", fields.get("maxSteps")))), 12, warnings)
    return AgentDefinition(name, description, prompt.strip(), _ordered(tools), _model(fields.get("model")),
                           effort, steps, background if isinstance(background, bool) else None,
                           read_only, source, tool, warnings)


def parse_file(path: Path, base: Path, tool: str) -> AgentDefinition | None:
    info = path.stat()
    if info.st_size > MAX_FILE_BYTES:
        raise DefinitionError(f"file exceeds {MAX_FILE_BYTES} bytes")
    text = path.read_text(encoding="utf-8")
    relative = path.relative_to(base).with_suffix("")
    if tool == "codex" or path.suffix == ".toml":
        if tomllib is None:
            raise DefinitionError("TOML support needs Python 3.11+")
        try:
            fields = tomllib.loads(text)
        except tomllib.TOMLDecodeError as exc:
            raise DefinitionError(f"invalid TOML: {exc}") from None
        return definition_from_fields(fields, "", name=relative.name, source=str(path), tool="codex")
    fields, body = split_frontmatter(text)
    default_name = relative.as_posix() if tool == "opencode" else relative.name
    return definition_from_fields(fields, body, name=default_name, source=str(path), tool=tool)


@dataclass
class AgentCatalog:
    definitions: dict[str, AgentDefinition] = field(default_factory=dict)
    duplicates: list[dict] = field(default_factory=list)
    skipped: list[dict] = field(default_factory=list)

    def get(self, name: str) -> AgentDefinition:
        definition = self.definitions.get(name)
        if definition is None and name == "explore":
            raise ValueError("The built-in explore agent was removed. Use general and put investigation-only "
                             "constraints in the prompt.")
        if definition is None:
            raise ValueError(f"Unknown subagent_type {name!r}. Available: {', '.join(self.definitions)}.")
        return definition

    def add(self, definition: AgentDefinition) -> None:
        kept = self.definitions.get(definition.name)
        if kept is not None:
            self.duplicates.append({"name": definition.name, "source": definition.source, "kept": kept.source})
            return
        if len(self.definitions) >= MAX_DEFINITIONS + len(BUILTINS):
            self.skipped.append({"path": definition.source, "reason": f"more than {MAX_DEFINITIONS} definitions"})
            return
        self.definitions[definition.name] = definition

    def items(self) -> list[dict]:
        return [d.to_dict() for d in self.definitions.values()]

    def warnings(self) -> list[str]:
        out = [f"{d.source}: {w}" for d in self.definitions.values() for w in d.warnings]
        out += [f"{s['path']}: {s['reason']}" for s in self.skipped]
        out += [f"{d['source']}: duplicate agent name {d['name']!r} (kept {d['kept']})" for d in self.duplicates]
        return out


def _scan(directory: Path):
    files, count = [], 0
    for root, dirs, names in os.walk(directory, followlinks=False):
        depth = len(Path(root).relative_to(directory).parts)
        dirs[:] = sorted(d for d in dirs if not d.startswith(".")) if depth < MAX_DEPTH else []
        for name in sorted(names):
            if name.endswith((".md", ".toml")) and not name.startswith("."):
                files.append(Path(root) / name)
                count += 1
                if count >= MAX_FILES_SCANNED:
                    return files
    return files


def load_catalog(workspace: str | Path, dirs=None, *, home: str | Path | None = None,
                 include_builtins: bool = True) -> AgentCatalog:
    """Load definitions. ``dirs`` None means every known location; otherwise exactly those dirs."""
    if dirs is None:
        locations = default_locations(workspace, home)
    else:
        if not isinstance(dirs, list) or not all(isinstance(d, str) for d in dirs) or len(dirs) > 64:
            raise ValueError("agents.dirs must be a list of at most 64 paths.")
        locations = [(Path(os.path.expanduser(d)), guess_format(Path(os.path.expanduser(d)))) for d in dirs]
    catalog = AgentCatalog()
    seen: set[Path] = set()
    for directory, tool in locations:
        if not directory.is_dir():
            continue
        resolved = directory.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)
        for path in _scan(resolved):
            wanted = ".toml" if tool == "codex" else ".md"
            if path.suffix != wanted:
                continue
            try:
                definition = parse_file(path, resolved, tool)
            except (OSError, UnicodeError, DefinitionError) as exc:
                catalog.skipped.append({"path": str(path), "reason": str(exc)[:300]})
                continue
            if definition is None:
                catalog.skipped.append({"path": str(path), "reason": "not a subagent (primary mode or disabled)"})
                continue
            catalog.add(definition)
    if include_builtins:
        for builtin in BUILTINS:
            catalog.add(AgentDefinition(**{**builtin.__dict__, "warnings": []}))
    return catalog
