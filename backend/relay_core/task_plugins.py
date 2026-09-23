# SPDX-License-Identifier: AGPL-3.0-or-later
"""Task plugins: versioned manifests for workspace kinds (#C0Q8, design #MEPR).

A task plugin changes what one Relay tab is *for*: which language the composer's router checks
(`router`), what runs a runnable line or builds the document (`runner`), which lazy tool group and
skills the pane's agent is offered (`tools`, `skills`), which panes the group fills (`panes`) and
how output is previewed (`preview`). It is not an MCP integration and never a Claude or Codex
plugin manifest: those describe agent extensions, and Relay does not execute them.

Each package is a folder holding ``plugin.json`` (docs/TASK-PLUGINS.md is the reference). Packages
come from three origins, and for one id the first wins, the others are reported as shadowed:

  project  <workspace>/.relay/plugins/<pkg>/ (and each parent up to the git root, nearest first)
  global   $XDG_CONFIG_HOME/relay/plugins/<pkg>/
  bundled  relay_core/plugins_bundled/<pkg>/

Lifecycle: discovered -> enabled -> active. Discovery reads files only: it never launches a
command, a kernel or a version probe. A project package that declares executable content (a
runner or tools) is *not* enabled until someone calls `enable`, which writes to Relay's own config
($XDG_CONFIG_HOME/relay/plugins.json), keyed by the project's resolved path and pinned to the
package's content digest — so a cloned repository cannot enable itself, and a pull that changes
an enabled package disables it until it is reviewed again. Bundled and global packages are enabled
by default. `activate` returns the workspace state a tab would adopt; `deactivate` returns the
default Bash state. Nothing in this module starts a process except `dependency_status(probe=True)`,
which runs each required program's version argument when explicitly asked.

    PYTHONPATH=backend python3 -m relay_core.task_plugins list --workspace .
"""
from __future__ import annotations

import argparse
import difflib
import fnmatch
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath

from .filelock import LOCK_EX, LOCK_UN, flock
from .instructions import git_root, project_dirs
from .tool_groups import GROUPS as NATIVE_TOOL_GROUPS

SCHEMA_VERSION = 1
MANIFEST = "plugin.json"
STATE_VERSION = 1
MAX_MANIFEST_BYTES = 64 * 1024
MAX_PACKAGE_FILES = 500
MAX_PACKAGE_BYTES = 8 * 1024 * 1024
MAX_PACKAGES_PER_DIR = 100
PROBE_TIMEOUT = 5.0

#: Precedence for one id: the first origin listed wins.
ORIGINS = ("project", "global", "bundled")
LANGUAGES = ("bash", "python", "stata", "tex")
RUNNER_KINDS = ("kernel", "artifact_command", "repl")
CWD_POLICIES = ("workspace", "project_root", "file_dir")
PANE_ROLES = ("editor", "console", "preview", "variables")
PREVIEW_ADAPTERS = ("pdf", "image", "svg", "html", "table")
#: Prefixes whose meaning every workspace keeps (`!` shell, `*` agent, `/` slash commands).
RESERVED_PREFIXES = ("!", "*", "/")
PLACEHOLDERS = ("file", "file_stem", "file_dir", "workspace", "connection_file")
#: Programs that turn their next argument back into a shell string.
SHELLS = {"sh", "bash", "zsh", "dash", "ksh", "mksh", "fish", "csh", "tcsh", "cmd", "powershell", "pwsh"}
SHELL_STRING_FLAGS = {"-c", "/c", "/k", "-command", "-encodedcommand", "-e"}
#: Environment names a runner may never ask for: `env` is an allowlist, not a way to reach secrets.
SECRET_ENV_PARTS = {"KEY", "APIKEY", "TOKEN", "SECRET", "PASSWORD", "PASSWD", "PASS", "CREDENTIAL",
                    "CREDENTIALS", "COOKIE", "AUTH", "SESSION", "PRIVATE"}
#: Tool-group names a plugin cannot take: Relay's own deferred groups and the verbs its native tools
#: start with, so `<group>_<name>` can never land on a native tool. The tool registry (t:9a) still
#: checks each name against the full native list when it registers a group.
RESERVED_GROUPS = set(NATIVE_TOOL_GROUPS) | {
    "run", "read", "write", "edit", "list", "load", "board", "app", "session", "skill", "skills",
    "memory", "media", "agent", "guest", "terminal", "command", "type", "set", "ask", "exit",
    "close", "update", "lookup", "file", "search", "web", "relay", "mcp"}

ID_RE = re.compile(r"^[a-z][a-z0-9-]{0,30}(\.[a-z][a-z0-9-]{0,30}){1,3}$")
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$")
PROGRAM_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]{0,63}$")
PROGRAM_PATTERN_RE = re.compile(r"^[A-Za-z0-9._+*?\[\]-]{1,64}$")
ENV_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]{0,63}$")
GROUP_RE = re.compile(r"^[a-z][a-z0-9]{0,15}$")
TOOL_RE = re.compile(r"^[a-z][a-z0-9]{0,15}_[a-z0-9_]{1,47}$")
LAYOUT_RE = re.compile(r"^[1-9](?::[1-9]){0,3}$")
PREFIX_RE = re.compile(r"^[^\sA-Za-z0-9]{1,3}$")
PLACEHOLDER_RE = re.compile(r"\{([^{}]*)\}")

DEFAULT_ROUTER = {"language": "bash", "prefixes": []}
DEFAULT_PANES = {"roles": ["console"], "layouts": ["1"], "default_layout": "1"}

# The keys each object accepts. An unknown key is an error with a suggestion, never ignored: a
# misspelt `requires` or `runner` would otherwise quietly drop a dependency check or a safety rule.
TOP_KEYS = ("schema_version", "id", "version", "name", "description", "activation", "router", "runner",
            "tools", "skills", "panes", "preview", "requires")
ACTIVATION_KEYS = ("files", "programs")
ROUTER_KEYS = ("language", "prefixes")
RUNNER_KEYS = ("kind", "command", "cwd", "env")
TOOLS_KEYS = ("group", "lazy", "items")
TOOL_ITEM_KEYS = ("name", "description")
PANES_KEYS = ("roles", "layouts", "default_layout")
PREVIEW_KEYS = ("adapter", "outputs")
REQUIRE_KEYS = ("program", "alternatives", "version_arg", "optional", "install_hint")


class PluginError(ValueError):
    """A lifecycle call that cannot proceed: unknown id, not enabled, missing programs."""


@dataclass(frozen=True)
class ManifestIssue:
    file: str
    field: str
    message: str
    fix: str = ""

    def __str__(self) -> str:
        text = f"{self.file}: {self.field or '(manifest)'}: {self.message}"
        return f"{text} Fix: {self.fix}" if self.fix else text

    def to_dict(self) -> dict:
        return {"file": self.file, "field": self.field, "message": self.message, "fix": self.fix}


class ManifestError(ValueError):
    def __init__(self, issues: list[ManifestIssue]):
        self.issues = list(issues)
        super().__init__("\n".join(str(i) for i in self.issues))


# --- the manifest --------------------------------------------------------------------------------

@dataclass(frozen=True)
class Activation:
    files: tuple[str, ...] = ()
    programs: tuple[str, ...] = ()


@dataclass(frozen=True)
class RouterSpec:
    language: str = "bash"
    prefixes: tuple[str, ...] = ()


@dataclass(frozen=True)
class RunnerSpec:
    kind: str
    command: tuple[str, ...]
    cwd: str = "workspace"
    env: tuple[str, ...] = ()


@dataclass(frozen=True)
class ToolItem:
    name: str
    description: str


@dataclass(frozen=True)
class ToolSpec:
    group: str
    items: tuple[ToolItem, ...]
    lazy: bool = True


@dataclass(frozen=True)
class PaneSpec:
    roles: tuple[str, ...] = ("console",)
    layouts: tuple[str, ...] = ("1",)
    default_layout: str = "1"


@dataclass(frozen=True)
class PreviewSpec:
    adapter: str
    outputs: tuple[str, ...]


@dataclass(frozen=True)
class Requirement:
    program: str
    alternatives: tuple[str, ...] = ()
    version_arg: tuple[str, ...] = ("--version",)
    optional: bool = False
    install_hint: str = ""

    @property
    def names(self) -> tuple[str, ...]:
        return (self.program, *self.alternatives)


@dataclass(frozen=True)
class Manifest:
    id: str
    version: str
    name: str
    description: str
    root: Path                     # resolved package folder
    activation: Activation = Activation()
    router: RouterSpec = RouterSpec()
    runner: RunnerSpec | None = None
    tools: ToolSpec | None = None
    skills: tuple[str, ...] = ()   # package-relative folders holding SKILL.md
    panes: PaneSpec = PaneSpec()
    preview: PreviewSpec | None = None
    requires: tuple[Requirement, ...] = ()

    @property
    def path(self) -> Path:
        return self.root / MANIFEST

    @property
    def executable(self) -> bool:
        """Does enabling this package let it run something? A runner launches a program and a tool
        group is code the agent can call; router, skills, panes and preview only change the UI."""
        return self.runner is not None or self.tools is not None

    def skill_dirs(self) -> list[Path]:
        return [self.root / rel for rel in self.skills]

    def to_dict(self) -> dict:
        return {
            "schema_version": SCHEMA_VERSION, "id": self.id, "version": self.version, "name": self.name,
            "description": self.description, "root": str(self.root),
            "activation": {"files": list(self.activation.files), "programs": list(self.activation.programs)},
            "router": router_dict(self),
            "runner": None if self.runner is None else {
                "kind": self.runner.kind, "command": list(self.runner.command), "cwd": self.runner.cwd,
                "env": list(self.runner.env)},
            "tools": None if self.tools is None else {
                "group": self.tools.group, "lazy": self.tools.lazy,
                "items": [{"name": t.name, "description": t.description} for t in self.tools.items]},
            "skills": list(self.skills),
            "panes": {"roles": list(self.panes.roles), "layouts": list(self.panes.layouts),
                      "default_layout": self.panes.default_layout},
            "preview": None if self.preview is None else {
                "adapter": self.preview.adapter, "outputs": list(self.preview.outputs)},
            "requires": [{"program": r.program, "alternatives": list(r.alternatives),
                          "version_arg": list(r.version_arg), "optional": r.optional,
                          "install_hint": r.install_hint} for r in self.requires],
            "executable": self.executable,
        }


def router_dict(manifest: Manifest | None) -> dict:
    if manifest is None:
        return {"language": DEFAULT_ROUTER["language"], "prefixes": list(DEFAULT_ROUTER["prefixes"])}
    return {"language": manifest.router.language, "prefixes": list(manifest.router.prefixes)}


class _Checker:
    """Collects every problem in one manifest, so a fix is one edit, not one run per mistake."""

    def __init__(self, file: str):
        self.file = file
        self.issues: list[ManifestIssue] = []

    def add(self, fieldname: str, message: str, fix: str = "") -> None:
        self.issues.append(ManifestIssue(self.file, fieldname, message, fix))

    def keys(self, obj: dict, allowed: tuple[str, ...], where: str) -> None:
        for key in obj:
            if key in allowed:
                continue
            close = difflib.get_close_matches(str(key), allowed, n=1, cutoff=0.6)
            fieldname = f"{where}.{key}" if where else str(key)
            if close:
                self.add(fieldname, "unknown key.", f"did you mean '{close[0]}'?")
            else:
                self.add(fieldname, "unknown key.", f"remove it; {where or 'the manifest'} accepts "
                         + ", ".join(allowed) + ".")

    def obj(self, value, fieldname: str, allowed: tuple[str, ...]) -> dict | None:
        if value is None:
            return None
        if not isinstance(value, dict):
            self.add(fieldname, f"must be an object, not {_kind(value)}.")
            return None
        self.keys(value, allowed, fieldname)
        return value

    def text(self, value, fieldname: str, required: bool = False, limit: int = 400) -> str:
        if value is None:
            if required:
                self.add(fieldname, "is required.", f"add \"{fieldname}\": \"...\".")
            return ""
        if not isinstance(value, str) or not value.strip():
            self.add(fieldname, f"must be a non-empty string, not {_kind(value)}.")
            return ""
        if len(value) > limit:
            self.add(fieldname, f"is {len(value)} characters; the limit is {limit}.", "shorten it.")
        return value.strip()

    def strings(self, value, fieldname: str) -> list[str]:
        if value is None:
            return []
        if isinstance(value, str):
            self.add(fieldname, "must be a list of strings, not a string.", f"write [{json.dumps(value)}].")
            return []
        if not isinstance(value, list):
            self.add(fieldname, f"must be a list of strings, not {_kind(value)}.")
            return []
        out = []
        for index, item in enumerate(value):
            if not isinstance(item, str) or not item:
                self.add(f"{fieldname}[{index}]", f"must be a non-empty string, not {_kind(item)}.")
            elif item in out:
                self.add(f"{fieldname}[{index}]", f"repeats {item!r}.", "remove the duplicate.")
            else:
                out.append(item)
        return out

    def flag(self, value, fieldname: str, default: bool) -> bool:
        if value is None:
            return default
        if type(value) is not bool:
            self.add(fieldname, f"must be true or false, not {_kind(value)}.")
            return default
        return value

    def choice(self, value, fieldname: str, choices: tuple[str, ...], default: str | None = None) -> str | None:
        if value is None:
            if default is None:
                self.add(fieldname, "is required.", "use one of " + ", ".join(choices) + ".")
            return default
        if value not in choices:
            close = difflib.get_close_matches(str(value), choices, n=1, cutoff=0.5)
            hint = f"did you mean '{close[0]}'? " if close else ""
            self.add(fieldname, f"{value!r} is not supported.", hint + "use one of " + ", ".join(choices) + ".")
            return default
        return value


def _kind(value) -> str:
    if value is None:
        return "null"
    return {bool: "a boolean", int: "a number", float: "a number", str: "a string", list: "a list",
            dict: "an object"}.get(type(value), type(value).__name__)


def _unsafe_relative(rel: str) -> str | None:
    """Why `rel` is not a safe relative path, or None. Separators are '/', on every platform."""
    if "\\" in rel:
        return "uses '\\'; write package paths with '/'."
    if "\0" in rel:
        return "contains a NUL byte."
    if rel.startswith("~") or rel.startswith("/") or re.match(r"^[A-Za-z]:", rel):
        return "is absolute; paths must be relative."
    if ".." in PurePosixPath(rel).parts:
        return "contains '..'; paths may not leave their folder."
    return None


def _package_path(check: _Checker, root: Path, rel, fieldname: str, want: str) -> Path | None:
    """A package-relative path that stays inside the package after symlinks, and exists as `want`."""
    if not isinstance(rel, str) or not rel.strip():
        check.add(fieldname, f"must be a relative path string, not {_kind(rel)}.")
        return None
    why = _unsafe_relative(rel)
    if why:
        check.add(fieldname, f"{rel!r} {why}", "use a path inside the package folder, e.g. \"skills/my-skill\".")
        return None
    target = (root / rel).resolve()
    if not _inside(target, root):
        check.add(fieldname, f"{rel!r} resolves outside the package ({target}).",
                  "replace the symlink with the real file inside the package.")
        return None
    if want == "dir" and not target.is_dir():
        check.add(fieldname, f"{rel!r} is not a folder in {root}.", "create it or correct the path.")
        return None
    if want == "file" and not target.is_file():
        check.add(fieldname, f"{rel!r} is not a file in {root}.", "create it or correct the path.")
        return None
    return target


def _glob(check: _Checker, pattern: str, fieldname: str) -> bool:
    why = _unsafe_relative(pattern)
    if why:
        check.add(fieldname, f"{pattern!r} {why}", "use a workspace-relative glob such as \"*.tex\".")
        return False
    return True


def _inside(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def _load_json(text: str, file: str) -> dict:
    def unique(pairs):
        out = {}
        for key, value in pairs:
            if key in out:
                raise ManifestError([ManifestIssue(file, key, "appears twice.", "keep one of them.")])
            out[key] = value
        return out
    try:
        data = json.loads(text, object_pairs_hook=unique)
    except json.JSONDecodeError as exc:
        raise ManifestError([ManifestIssue(f"{file}:{exc.lineno}:{exc.colno}", "",
                                           f"is not valid JSON ({exc.msg}).",
                                           "fix the syntax; comments and trailing commas are not JSON.")]) from None
    if not isinstance(data, dict):
        raise ManifestError([ManifestIssue(file, "", f"must be a JSON object, not {_kind(data)}.")])
    return data


def parse_manifest(data: dict, root: str | Path, file: str | None = None) -> Manifest:
    """Validate one manifest's data against schema v1; raise ManifestError listing every issue."""
    root = Path(root).resolve()
    file = file or str(root / MANIFEST)
    check = _Checker(file)
    if not isinstance(data, dict):
        raise ManifestError([ManifestIssue(file, "", f"must be a JSON object, not {_kind(data)}.")])
    version = data.get("schema_version")
    if version is None:
        raise ManifestError([ManifestIssue(file, "schema_version", "is missing.",
                                           f"add \"schema_version\": {SCHEMA_VERSION} as the first key.")])
    if type(version) is not int or version != SCHEMA_VERSION:
        # An unknown version is not guessed at: its keys may mean something this Relay cannot check.
        raise ManifestError([ManifestIssue(file, "schema_version", f"{version!r} is not a schema this Relay reads "
                                           f"(it reads {SCHEMA_VERSION}).",
                                           "update Relay, or write the manifest for schema_version 1.")])
    check.keys(data, TOP_KEYS, "")

    plugin_id = data.get("id")
    if not isinstance(plugin_id, str) or not ID_RE.match(plugin_id) or len(plugin_id) > 64:
        check.add("id", f"{plugin_id!r} is not a namespaced id.",
                  "use lowercase dotted segments such as \"yourname.sql\" (2-4 segments, letters, digits, '-').")
        plugin_id = str(plugin_id)
    plugin_version = data.get("version")
    if not isinstance(plugin_version, str) or not VERSION_RE.match(plugin_version):
        check.add("version", f"{plugin_version!r} is not a semantic version.", "write \"MAJOR.MINOR.PATCH\", e.g. \"0.1.0\".")
        plugin_version = str(plugin_version)
    name = check.text(data.get("name"), "name", required=True, limit=80)
    description = check.text(data.get("description"), "description", required=True, limit=400)

    # activation
    activation = Activation()
    raw = check.obj(data.get("activation"), "activation", ACTIVATION_KEYS)
    if raw is not None:
        files = [p for i, p in enumerate(check.strings(raw.get("files"), "activation.files"))
                 if _glob(check, p, f"activation.files[{i}]")]
        programs = []
        for i, p in enumerate(check.strings(raw.get("programs"), "activation.programs")):
            if PROGRAM_PATTERN_RE.match(p):
                programs.append(p)
            else:
                check.add(f"activation.programs[{i}]", f"{p!r} is not a program name.",
                          "give the basename (\"ipython\", \"python3*\"), not a path or a command line.")
        activation = Activation(tuple(files), tuple(programs))

    # router
    router = RouterSpec()
    raw = check.obj(data.get("router"), "router", ROUTER_KEYS)
    if raw is not None:
        language = check.choice(raw.get("language"), "router.language", LANGUAGES)
        prefixes = []
        for i, p in enumerate(check.strings(raw.get("prefixes"), "router.prefixes")):
            if p in RESERVED_PREFIXES or p[:1] in RESERVED_PREFIXES:
                check.add(f"router.prefixes[{i}]", f"{p!r} is reserved: '!' always forces the shell, '*' the agent "
                          "and '/' starts a Relay command.", "remove it; prefixes only add language routes such as \"%\".")
            elif not PREFIX_RE.match(p):
                check.add(f"router.prefixes[{i}]", f"{p!r} is not a prefix.", "use 1-3 punctuation characters, e.g. \"%\".")
            else:
                prefixes.append(p)
        router = RouterSpec(language or "bash", tuple(prefixes))

    # requires (before runner: the runner's program must be one of them)
    requires: list[Requirement] = []
    raw_requires = data.get("requires")
    if raw_requires is not None and not isinstance(raw_requires, list):
        check.add("requires", f"must be a list of objects, not {_kind(raw_requires)}.")
        raw_requires = []
    for i, item in enumerate(raw_requires or []):
        where = f"requires[{i}]"
        item = check.obj(item, where, REQUIRE_KEYS)
        if item is None:
            continue
        program = item.get("program")
        if not isinstance(program, str) or not PROGRAM_RE.match(program):
            check.add(f"{where}.program", f"{program!r} is not a program name.",
                      "give the name looked up on PATH, e.g. \"latexmk\"; no paths or arguments.")
            continue
        alternatives = []
        for j, alt in enumerate(check.strings(item.get("alternatives"), f"{where}.alternatives")):
            if PROGRAM_RE.match(alt):
                alternatives.append(alt)
            else:
                check.add(f"{where}.alternatives[{j}]", f"{alt!r} is not a program name.")
        version_arg = item.get("version_arg", ["--version"])
        if isinstance(version_arg, str):
            version_arg = [version_arg]
        if not isinstance(version_arg, list) or not all(isinstance(a, str) and a for a in version_arg):
            check.add(f"{where}.version_arg", "must be a string or a list of strings.", "e.g. \"--version\".")
            version_arg = ["--version"]
        requires.append(Requirement(program, tuple(alternatives), tuple(version_arg),
                                    check.flag(item.get("optional"), f"{where}.optional", False),
                                    check.text(item.get("install_hint"), f"{where}.install_hint", limit=200)))
    required_names = {n for r in requires for n in r.names}

    # runner
    runner = None
    raw = check.obj(data.get("runner"), "runner", RUNNER_KEYS)
    if raw is not None:
        kind = check.choice(raw.get("kind"), "runner.kind", RUNNER_KINDS)
        command = _command(check, raw.get("command"), root, kind, required_names)
        cwd = check.choice(raw.get("cwd"), "runner.cwd", CWD_POLICIES, "workspace")
        env = []
        for i, name_ in enumerate(check.strings(raw.get("env"), "runner.env")):
            if not ENV_RE.match(name_):
                check.add(f"runner.env[{i}]", f"{name_!r} is not an environment variable name.")
            elif SECRET_ENV_PARTS & set(name_.upper().split("_")):
                check.add(f"runner.env[{i}]", f"{name_!r} looks like a credential; the runner environment is an "
                          "allowlist and never passes secrets.",
                          "remove it; a tool that needs a key gets it from Relay's keystore, not the environment.")
            else:
                env.append(name_)
        if kind and command:
            runner = RunnerSpec(kind, tuple(command), cwd or "workspace", tuple(env))

    # tools
    tools = None
    raw = check.obj(data.get("tools"), "tools", TOOLS_KEYS)
    if raw is not None:
        group = raw.get("group")
        if not isinstance(group, str) or not GROUP_RE.match(group):
            check.add("tools.group", f"{group!r} is not a tool-group name.", "use 1-16 lowercase letters/digits, e.g. \"tex\".")
            group = None
        elif group in RESERVED_GROUPS:
            check.add("tools.group", f"{group!r} is one of Relay's own tool groups or tool prefixes.",
                      "choose a name for your workspace, e.g. \"sql\".")
            group = None
        lazy = check.flag(raw.get("lazy"), "tools.lazy", True)
        if not lazy:
            check.add("tools.lazy", "schema v1 tool groups are always loaded on demand.", "remove \"lazy\" or set it to true.")
        items = []
        raw_items = raw.get("items")
        if not isinstance(raw_items, list) or not raw_items:
            check.add("tools.items", "must be a non-empty list of {name, description}.")
            raw_items = []
        for i, item in enumerate(raw_items):
            where = f"tools.items[{i}]"
            item = check.obj(item, where, TOOL_ITEM_KEYS)
            if item is None:
                continue
            tool = item.get("name")
            if not isinstance(tool, str) or not TOOL_RE.match(tool):
                check.add(f"{where}.name", f"{tool!r} is not a tool name.",
                          f"use \"{group or 'group'}_<verb>\" in lowercase, e.g. \"{group or 'group'}_build\".")
                continue
            if group and not tool.startswith(group + "_"):
                check.add(f"{where}.name", f"{tool!r} is not in the '{group}' namespace.",
                          f"rename it \"{group}_{tool.split('_', 1)[-1]}\".")
                continue
            if any(t.name == tool for t in items):
                check.add(f"{where}.name", f"{tool!r} is declared twice.", "remove the duplicate.")
                continue
            items.append(ToolItem(tool, check.text(item.get("description"), f"{where}.description", True, 300)))
        if group and items:
            tools = ToolSpec(group, tuple(items), True)

    # skills
    skills = []
    raw_skills = data.get("skills")
    if raw_skills is not None and not isinstance(raw_skills, list):
        check.add("skills", f"must be a list of package-relative folders, not {_kind(raw_skills)}.")
        raw_skills = []
    for i, rel in enumerate(raw_skills or []):
        target = _package_path(check, root, rel, f"skills[{i}]", "dir")
        if target is None:
            continue
        manifest = target / "SKILL.md"
        if not manifest.is_file():
            check.add(f"skills[{i}]", f"{rel!r} has no SKILL.md.", "point at the folder that holds SKILL.md.")
        elif not _inside(manifest.resolve(), root):
            check.add(f"skills[{i}]", f"{rel}/SKILL.md links outside the package.", "replace the symlink with the file.")
        elif rel in skills:
            check.add(f"skills[{i}]", f"repeats {rel!r}.", "remove the duplicate.")
        else:
            skills.append(rel)

    # panes
    panes = PaneSpec()
    raw = check.obj(data.get("panes"), "panes", PANES_KEYS)
    if raw is not None:
        roles = []
        for i, role in enumerate(check.strings(raw.get("roles"), "panes.roles")):
            if check.choice(role, f"panes.roles[{i}]", PANE_ROLES, ""):
                roles.append(role)
        if raw.get("roles") is None:
            roles = ["console"]
        if roles and "console" not in roles:
            check.add("panes.roles", "must include \"console\": the tab's terminal stays in every workspace.",
                      "add \"console\".")
        layouts = []
        for i, layout in enumerate(check.strings(raw.get("layouts"), "panes.layouts")):
            if not LAYOUT_RE.match(layout):
                check.add(f"panes.layouts[{i}]", f"{layout!r} is not a layout preset.",
                          "write column weights joined by ':', e.g. \"1:1:1\" or \"2:1\".")
            elif len(layout.split(":")) > max(1, len(roles)):
                check.add(f"panes.layouts[{i}]", f"{layout!r} has {len(layout.split(':'))} columns for "
                          f"{len(roles)} role(s).", "use at most one column per role.")
            else:
                layouts.append(layout)
        if not layouts:
            layouts = [":".join("1" for _ in (roles or ["console"]))]
        default_layout = raw.get("default_layout", layouts[0])
        if default_layout not in layouts:
            check.add("panes.default_layout", f"{default_layout!r} is not one of panes.layouts.",
                      "use one of " + ", ".join(layouts) + ".")
            default_layout = layouts[0]
        panes = PaneSpec(tuple(roles or ["console"]), tuple(layouts), default_layout)

    # preview
    preview = None
    raw = check.obj(data.get("preview"), "preview", PREVIEW_KEYS)
    if raw is not None:
        adapter = check.choice(raw.get("adapter"), "preview.adapter", PREVIEW_ADAPTERS)
        outputs = [p for i, p in enumerate(check.strings(raw.get("outputs"), "preview.outputs"))
                   if _glob(check, p, f"preview.outputs[{i}]")]
        if not outputs and raw.get("outputs") is None:
            check.add("preview.outputs", "is required.", "list the output globs, e.g. [\"*.pdf\"].")
        if adapter and outputs:
            preview = PreviewSpec(adapter, tuple(outputs))
        if "preview" not in panes.roles and preview is not None:
            check.add("panes.roles", "declares a preview adapter but no \"preview\" pane.", "add \"preview\" to panes.roles.")

    if check.issues:
        raise ManifestError(check.issues)
    return Manifest(plugin_id, plugin_version, name, description, root, activation, router, runner, tools,
                    tuple(skills), panes, preview, tuple(requires))


def _command(check: _Checker, command, root: Path, kind: str | None, required_names: set[str]) -> list[str]:
    if command is None:
        check.add("runner.command", "is required.", "give the program and its arguments as a list, e.g. [\"latexmk\", \"-pdf\", \"{file}\"].")
        return []
    if isinstance(command, str):
        try:
            suggestion = json.dumps(shlex.split(command))
        except ValueError:
            suggestion = "[\"program\", \"arg\", ...]"
        check.add("runner.command", "is a shell string; Relay never runs a command through a shell.",
                  f"write it as an argv list: {suggestion}.")
        return []
    if not isinstance(command, list) or not command:
        check.add("runner.command", f"must be a non-empty list of strings, not {_kind(command)}.")
        return []
    ok = True
    for i, arg in enumerate(command):
        if not isinstance(arg, str) or not arg or "\0" in arg:
            check.add(f"runner.command[{i}]", f"must be a non-empty string, not {_kind(arg)}.")
            ok = False
            continue
        # "{{" and "}}" are literal braces, as in str.format.
        for name in PLACEHOLDER_RE.findall(arg.replace("{{", "").replace("}}", "")):
            if name not in PLACEHOLDERS:
                check.add(f"runner.command[{i}]", f"{{{name}}} is not a placeholder.",
                          "use one of " + ", ".join("{" + p + "}" for p in PLACEHOLDERS) + ".")
                ok = False
            elif name == "connection_file" and kind != "kernel":
                check.add(f"runner.command[{i}]", "{connection_file} exists only for kind \"kernel\".")
                ok = False
    if not ok:
        return []
    program = command[0]
    if PLACEHOLDER_RE.search(program):
        check.add("runner.command[0]", "the program may not be a placeholder.", "name the program itself.")
        return []
    base = PurePosixPath(program.replace("\\", "/")).name.lower().removesuffix(".exe")
    if "/" in program or "\\" in program:
        if not program.startswith("./"):
            check.add("runner.command[0]", f"{program!r} is a path outside the package.",
                      f"use the program name (\"{base}\") and list it in requires, or a \"./\" path inside the package.")
            return []
        if _package_path(check, root, program[2:], "runner.command[0]", "file") is None:
            return []
    elif program not in required_names:
        check.add("runner.command[0]", f"{program!r} is not listed in requires.",
                  f"add {{\"program\": \"{program}\"}} to requires, so a missing program is reported before activation.")
        return []
    if base == "env":
        check.add("runner.command[0]", "\"env\" re-dispatches to another program.",
                  "name the program directly and list variables in runner.env.")
        return []
    if base in SHELLS and any(a.lower() in SHELL_STRING_FLAGS for a in command[1:]):
        check.add("runner.command", f"runs {base} with a command string, which is a shell string with extra steps.",
                  "name the real program and its arguments as the argv list.")
        return []
    return list(command)


def load_manifest(package: str | Path) -> Manifest:
    """Read and validate `<package>/plugin.json`. Raises ManifestError."""
    root = Path(package)
    path = root / MANIFEST
    file = str(path)
    if path.is_symlink() or (path.exists() and not _inside(path.resolve(), root.resolve())):
        raise ManifestError([ManifestIssue(file, "", "is a symlink.", "replace it with the manifest file itself.")])
    if not path.is_file():
        raise ManifestError([ManifestIssue(file, "", "does not exist.", f"create {MANIFEST} in the package folder.")])
    try:
        size = path.stat().st_size
        if size > MAX_MANIFEST_BYTES:
            raise ManifestError([ManifestIssue(file, "", f"is {size} bytes; the limit is {MAX_MANIFEST_BYTES}.")])
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        raise ManifestError([ManifestIssue(file, "", f"cannot be read ({exc}).")]) from None
    return parse_manifest(_load_json(text, file), root, file)


def package_digest(root: str | Path) -> str:
    """sha256 over every file in the package (paths and bytes), the pin an enable is recorded against."""
    root = Path(root).resolve()
    digest = hashlib.sha256()
    count = total = 0
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if d not in ("__pycache__", ".git"))
        for name in sorted(filenames):
            path = Path(dirpath) / name
            rel = path.relative_to(root).as_posix()
            real = path.resolve()
            if not _inside(real, root):
                raise ManifestError([ManifestIssue(str(path), "", "links outside the package.",
                                                   "replace the symlink with the real file.")])
            count += 1
            size = real.stat().st_size
            total += size
            if count > MAX_PACKAGE_FILES or total > MAX_PACKAGE_BYTES:
                raise ManifestError([ManifestIssue(str(root), "", f"holds more than {MAX_PACKAGE_FILES} files or "
                                                   f"{MAX_PACKAGE_BYTES // (1024 * 1024)} MiB.",
                                                   "keep build outputs and data out of the plugin folder.")])
            digest.update(rel.encode("utf-8") + b"\0" + str(size).encode() + b"\0")
            with real.open("rb") as handle:
                for chunk in iter(lambda: handle.read(65536), b""):
                    digest.update(chunk)
    return "sha256:" + digest.hexdigest()


# --- dependencies --------------------------------------------------------------------------------

@dataclass
class DependencyStatus:
    program: str
    optional: bool
    found: str | None = None       # which of program/alternatives was found
    path: str | None = None
    version: str | None = None     # only when probed
    error: str = ""
    install_hint: str = ""

    @property
    def missing(self) -> bool:
        return self.path is None

    def to_dict(self) -> dict:
        return {"program": self.program, "optional": self.optional, "found": self.found, "path": self.path,
                "version": self.version, "error": self.error, "install_hint": self.install_hint,
                "missing": self.missing}


def dependency_status(manifest: Manifest, probe: bool = False, which=shutil.which,
                      timeout: float = PROBE_TIMEOUT) -> list[DependencyStatus]:
    """Resolve each required program on PATH. Only `probe=True` runs anything: the version argument."""
    out = []
    for req in manifest.requires:
        status = DependencyStatus(req.program, req.optional, install_hint=req.install_hint)
        for name in req.names:
            path = which(name)
            if path:
                status.found, status.path = name, path
                break
        if status.path is None:
            status.error = "not found on PATH (looked for " + ", ".join(req.names) + ")"
        elif probe and not req.version_arg:
            status.error = "no version probe declared"
        elif probe:
            status.version, status.error = _probe(status.path, req.version_arg, timeout)
        out.append(status)
    return out


def _probe(path: str, args: tuple[str, ...], timeout: float) -> tuple[str | None, str]:
    env = {k: v for k, v in os.environ.items() if k in ("PATH", "HOME", "LANG", "LC_ALL", "SYSTEMROOT", "TMPDIR", "TEMP")}
    try:
        done = subprocess.run([path, *args], capture_output=True, text=True, timeout=timeout,
                              stdin=subprocess.DEVNULL, env=env, errors="replace")
    except subprocess.TimeoutExpired:
        return None, f"version probe timed out after {timeout:g}s"
    except OSError as exc:
        return None, f"version probe failed: {exc}"
    for stream in (done.stdout, done.stderr):
        for line in stream.splitlines():
            if line.strip():
                return line.strip()[:200], "" if done.returncode == 0 else f"exit status {done.returncode}"
    return None, f"no version output (exit status {done.returncode})"


# --- discovery -----------------------------------------------------------------------------------

def relay_config_dir() -> Path:
    """$XDG_CONFIG_HOME/relay, the same root skills.refined_dir and instructions.default_target use."""
    base = os.environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")
    return Path(base) / "relay"


def bundled_dir() -> Path:
    """Plugins that ship with Relay, beside the package like skills_bundled (CMake installs backend/)."""
    return Path(__file__).resolve().parent / "plugins_bundled"


def global_dir() -> Path:
    return relay_config_dir() / "plugins"


def state_path() -> Path:
    """Enablement state: in Relay's config, never in the repository (RELAY_PLUGIN_STATE overrides)."""
    override = os.environ.get("RELAY_PLUGIN_STATE")
    return Path(override) if override else relay_config_dir() / "plugins.json"


def project_key(workspace: str | Path) -> str:
    """The project a workspace belongs to: its git root, resolved. A clone elsewhere is another project."""
    return str(git_root(Path(workspace).expanduser().resolve()))


def project_plugin_dirs(workspace: str | Path) -> list[Path]:
    """<dir>/.relay/plugins for the workspace and each parent up to the git root, nearest first."""
    workspace = Path(workspace).expanduser().resolve()
    return [d / ".relay" / "plugins" for d in reversed(project_dirs(workspace))]


@dataclass
class PluginRecord:
    """One package folder found on disk, valid or not."""
    origin: str
    root: Path
    manifest: Manifest | None = None
    issues: list[ManifestIssue] = field(default_factory=list)
    digest: str = ""
    shadowed_by: "PluginRecord | None" = None
    shadows: list["PluginRecord"] = field(default_factory=list)

    @property
    def id(self) -> str:
        return self.manifest.id if self.manifest else self.root.name

    @property
    def valid(self) -> bool:
        return self.manifest is not None and not self.issues


@dataclass
class Discovery:
    plugins: dict[str, PluginRecord] = field(default_factory=dict)   # id -> the winning record
    shadowed: list[PluginRecord] = field(default_factory=list)
    invalid: list[PluginRecord] = field(default_factory=list)


def _scan_dir(directory: Path, origin: str) -> list[PluginRecord]:
    if not directory.is_dir():
        return []
    base = directory.resolve()
    records = []
    for entry in sorted(base.iterdir(), key=lambda p: p.name):
        if not entry.is_dir() or entry.name.startswith(".") or entry.name == "__pycache__":
            continue
        if not (entry / MANIFEST).exists() and not (entry / MANIFEST).is_symlink():
            continue
        if len(records) >= MAX_PACKAGES_PER_DIR:
            records.append(PluginRecord(origin, entry, issues=[ManifestIssue(str(base), "", f"holds more than "
                                        f"{MAX_PACKAGES_PER_DIR} packages; the rest are not read.")]))
            break
        root = entry.resolve()
        if root.parent != base:
            records.append(PluginRecord(origin, entry, issues=[ManifestIssue(str(entry), "", "links outside "
                                        f"{base}.", "move the package into the plugins folder itself.")]))
            continue
        record = PluginRecord(origin, root)
        try:
            record.manifest = load_manifest(root)
            record.digest = package_digest(root)
        except ManifestError as exc:
            record.manifest, record.issues = None, exc.issues
        records.append(record)
    return records


def discover(workspace: str | Path | None = None, *, bundled: Path | None = None,
             global_plugins: Path | None = None) -> Discovery:
    """Every package in every origin; for one id project wins over global over bundled."""
    found: list[PluginRecord] = []
    if workspace is not None:
        for directory in project_plugin_dirs(workspace):
            found += _scan_dir(directory, "project")
    found += _scan_dir(global_plugins or global_dir(), "global")
    found += _scan_dir(bundled or bundled_dir(), "bundled")
    result = Discovery()
    bundled_ids = {r.id for r in found if r.origin == "bundled" and r.valid}
    for record in found:
        if record.valid and record.origin != "bundled" and record.id.startswith("relay.") \
                and record.id not in bundled_ids:
            record.issues.append(ManifestIssue(str(record.root / MANIFEST), "id",
                                               f"{record.id!r} uses the relay. namespace, which is reserved for "
                                               "plugins Relay ships (overriding one of them is allowed).",
                                               "use your own namespace, e.g. \"yourname." + record.id.split('.', 1)[1] + "\"."))
        if not record.valid:
            result.invalid.append(record)
            continue
        winner = result.plugins.get(record.id)
        if winner is None:
            result.plugins[record.id] = record
        else:
            record.shadowed_by = winner
            winner.shadows.append(record)
            result.shadowed.append(record)
    return result


# --- enablement ----------------------------------------------------------------------------------

@dataclass(frozen=True)
class Enablement:
    enabled: bool
    reason: str
    explicit: bool = False


def _read_state(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return {"version": STATE_VERSION, "projects": {}}
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise PluginError(f"{path}: cannot read plugin state ({exc}); fix or remove the file.") from None
    if not isinstance(data, dict) or data.get("version") != STATE_VERSION or not isinstance(data.get("projects"), dict):
        raise PluginError(f"{path}: not a version-{STATE_VERSION} plugin state file; fix or remove it.")
    return data


def _write_state(path: Path, mutate) -> dict:
    """Read-modify-write under a sibling lock, replaced atomically, so two windows cannot lose an enable."""
    path.parent.mkdir(parents=True, exist_ok=True)
    lock_path = path.with_name(path.name + ".lock")
    with open(lock_path, "a+") as lock:
        flock(lock, LOCK_EX)
        try:
            data = _read_state(path)
            mutate(data)
            temp = path.with_name(f"{path.name}.{os.getpid()}.tmp")
            temp.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            os.replace(temp, path)
            return data
        finally:
            flock(lock, LOCK_UN)


def enablement(record: PluginRecord, project: str, state: dict) -> Enablement:
    if not record.valid:
        return Enablement(False, "invalid: " + "; ".join(str(i) for i in record.issues))
    entry = state.get("projects", {}).get(project, {}).get(record.id, {}).get(record.origin)
    if isinstance(entry, dict) and entry.get("enabled") is False:
        return Enablement(False, "disabled for this project", True)
    if isinstance(entry, dict) and entry.get("enabled") is True:
        if record.origin == "project" and entry.get("digest") != record.digest:
            return Enablement(False, "changed since it was enabled (the package's files differ from what was "
                              "reviewed); review it and enable it again", True)
        return Enablement(True, f"enabled for this project on {entry.get('at', '?')}", True)
    if record.origin == "project" and record.manifest.executable:
        what = " and ".join(w for w, on in (("a runner command", record.manifest.runner is not None),
                                            ("tools", record.manifest.tools is not None)) if on)
        return Enablement(False, f"project package declares {what}; it runs only after you enable it")
    return Enablement(True, f"{record.origin} package, enabled by default")


# --- activation ----------------------------------------------------------------------------------

@dataclass
class ActivationState:
    """What a tab adopts: the router it shows before execution, the runner it *would* start, its panes."""
    workspace: str
    tab: str | None
    plugin_id: str | None = None
    origin: str | None = None
    state: str = "inactive"                      # "active" | "inactive"
    router: dict = field(default_factory=lambda: router_dict(None))
    runner: dict | None = None
    tools: dict | None = None
    skills: list[str] = field(default_factory=list)
    panes: dict = field(default_factory=lambda: {k: (list(v) if isinstance(v, list) else v)
                                                 for k, v in DEFAULT_PANES.items()})
    preview: dict | None = None
    dependencies: list[dict] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)

    def to_dict(self) -> dict:
        return {"workspace": self.workspace, "tab": self.tab, "plugin_id": self.plugin_id, "origin": self.origin,
                "state": self.state, "router": self.router, "runner": self.runner, "tools": self.tools,
                "skills": self.skills, "panes": self.panes, "preview": self.preview,
                "dependencies": self.dependencies, "notes": self.notes}


@dataclass(frozen=True)
class Candidate:
    plugin_id: str
    origin: str
    reason: str
    enabled: bool
    enable_reason: str
    missing: tuple[str, ...] = ()

    def to_dict(self) -> dict:
        return {"plugin_id": self.plugin_id, "origin": self.origin, "reason": self.reason, "enabled": self.enabled,
                "enable_reason": self.enable_reason, "missing": list(self.missing)}


def match_activation(manifest: Manifest, path: str | Path | None = None, foreground_program=None,
                     relative_to: str | Path | None = None) -> list[str]:
    """Why `manifest`'s activation rules select this file or foreground program ([] = they do not).

    A glob without '/' is matched against the file name; one with '/' against the path relative to
    `relative_to` (the project root). A program pattern is matched against the basename of the
    foreground argv[0], without a Windows ".exe".
    """
    reasons = []
    if path is not None:
        target = Path(path)
        rel = None
        if relative_to is not None:
            try:
                rel = target.resolve().relative_to(Path(relative_to).resolve()).as_posix()
            except ValueError:
                rel = None
        for pattern in manifest.activation.files:
            subject = rel if "/" in pattern else target.name
            if subject is not None and fnmatch.fnmatch(subject, pattern):
                reasons.append(f"file {subject!r} matches activation.files {pattern!r}")
                break
    program = _program_name(foreground_program)
    if program:
        for pattern in manifest.activation.programs:
            if fnmatch.fnmatchcase(program, pattern):
                reasons.append(f"foreground program {program!r} matches activation.programs {pattern!r}")
                break
    return reasons


def _program_name(foreground) -> str:
    if not foreground:
        return ""
    if isinstance(foreground, str):
        try:
            # posix=False keeps a Windows path's backslashes; its quotes are stripped below.
            argv = shlex.split(foreground, posix="\\" not in foreground)
        except ValueError:
            argv = foreground.split()
        argv = [a.strip("\"'") for a in argv]
    else:
        argv = [str(a) for a in foreground]
    if not argv:
        return ""
    name = PurePosixPath(argv[0].replace("\\", "/")).name
    return name[:-4] if name.lower().endswith(".exe") else name


class PluginRegistry:
    """Discovery, enablement and per-tab activation over the three origins. Launches nothing."""

    def __init__(self, *, bundled: str | Path | None = None, global_plugins: str | Path | None = None,
                 state: str | Path | None = None, which=shutil.which):
        self.bundled = Path(bundled) if bundled else bundled_dir()
        self.global_plugins = Path(global_plugins) if global_plugins else global_dir()
        self.state_file = Path(state) if state else state_path()
        self.which = which
        self._active: dict[tuple[str, str | None], ActivationState] = {}

    # discovery
    def discover(self, workspace: str | Path | None = None) -> Discovery:
        return discover(workspace, bundled=self.bundled, global_plugins=self.global_plugins)

    def record(self, workspace: str | Path | None, plugin_id: str) -> PluginRecord:
        found = self.discover(workspace)
        record = found.plugins.get(plugin_id)
        if record is not None:
            return record
        broken = [r for r in found.invalid if r.id == plugin_id]
        if broken:
            raise PluginError(f"{plugin_id} is invalid:\n" + "\n".join(str(i) for i in broken[0].issues))
        close = difflib.get_close_matches(plugin_id, list(found.plugins), n=1, cutoff=0.5)
        hint = f" Did you mean {close[0]}?" if close else ""
        raise PluginError(f"no task plugin {plugin_id!r} for this workspace.{hint} Known: "
                          + (", ".join(sorted(found.plugins)) or "none") + ".")

    def list(self, workspace: str | Path | None = None, probe: bool = False) -> list[dict]:
        """Every package with origin, enablement, shadowing and dependency status (probe only if asked)."""
        found = self.discover(workspace)
        state = _read_state(self.state_file)
        project = project_key(workspace) if workspace is not None else ""
        rows = []
        for record in [*found.plugins.values(), *found.shadowed, *found.invalid]:
            row = {"id": record.id, "origin": record.origin, "root": str(record.root), "valid": record.valid,
                   "issues": [i.to_dict() for i in record.issues],
                   "shadowed_by": None if record.shadowed_by is None else
                   {"origin": record.shadowed_by.origin, "root": str(record.shadowed_by.root)},
                   "shadows": [{"origin": s.origin, "root": str(s.root)} for s in record.shadows]}
            if record.valid:
                ability = enablement(record, project, state)
                deps = dependency_status(record.manifest, probe=probe, which=self.which)
                row.update({"name": record.manifest.name, "version": record.manifest.version,
                            "description": record.manifest.description,
                            "executable": record.manifest.executable, "digest": record.digest,
                            "enabled": ability.enabled and record.shadowed_by is None,
                            "enable_reason": "shadowed by the " + record.shadowed_by.origin + " package"
                            if record.shadowed_by else ability.reason,
                            "router": router_dict(record.manifest),
                            "dependencies": [d.to_dict() for d in deps],
                            "missing": [d.program for d in deps if d.missing and not d.optional]})
            else:
                row.update({"enabled": False, "enable_reason": "invalid", "missing": []})
            rows.append(row)
        return rows

    # enablement
    def enablement(self, workspace: str | Path, plugin_id: str) -> Enablement:
        return enablement(self.record(workspace, plugin_id), project_key(workspace), _read_state(self.state_file))

    def enable(self, workspace: str | Path, plugin_id: str) -> Enablement:
        """The deliberate act that lets a project package run: recorded outside the repo, pinned to its digest."""
        return self._set(workspace, plugin_id, True)

    def disable(self, workspace: str | Path, plugin_id: str) -> Enablement:
        return self._set(workspace, plugin_id, False)

    def _set(self, workspace, plugin_id: str, enabled: bool) -> Enablement:
        record = self.record(workspace, plugin_id)
        project = project_key(workspace)
        entry = {"enabled": enabled, "at": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "version": record.manifest.version,
                 "root": str(record.root)}
        if record.origin == "project":
            entry["digest"] = record.digest

        def mutate(data):
            data.setdefault("projects", {}).setdefault(project, {}).setdefault(plugin_id, {})[record.origin] = entry
        state = _write_state(self.state_file, mutate)
        if not enabled:
            for key in [k for k in self._active if k[0] == project and self._active[k].plugin_id == plugin_id]:
                del self._active[key]
        return enablement(record, project, state)

    # activation
    def activate(self, workspace: str | Path, plugin_id: str, tab: str | None = None) -> ActivationState:
        """The state a tab adopts for `plugin_id`. Refuses a disabled plugin or a missing required program."""
        record = self.record(workspace, plugin_id)
        project = project_key(workspace)
        ability = enablement(record, project, _read_state(self.state_file))
        if not ability.enabled:
            raise PluginError(f"{plugin_id} ({record.origin}, {record.root}) is not enabled: {ability.reason}. "
                              f"Enable it with: python3 -m relay_core.task_plugins enable {plugin_id} "
                              f"--workspace {project}")
        manifest = record.manifest
        deps = dependency_status(manifest, which=self.which)
        missing = [d for d in deps if d.missing and not d.optional]
        if missing:
            raise PluginError(f"{plugin_id} needs " + "; ".join(
                f"{d.program}" + (f" ({d.install_hint})" if d.install_hint else "") for d in missing)
                + " on PATH before it can be activated.")
        key = (project, tab)
        notes = []
        previous = self._active.get(key)
        if previous is not None and previous.plugin_id != plugin_id:
            notes.append(f"replaced {previous.plugin_id}")
        notes += [f"optional {d.program} not found" for d in deps if d.missing and d.optional]
        data = manifest.to_dict()
        state = ActivationState(str(Path(workspace).expanduser().resolve()), tab, plugin_id, record.origin, "active",
                                router_dict(manifest), data["runner"], data["tools"],
                                [str(p) for p in manifest.skill_dirs()], data["panes"], data["preview"],
                                [d.to_dict() for d in deps], notes)
        self._active[key] = state
        return state

    def deactivate(self, workspace: str | Path, tab: str | None = None) -> ActivationState:
        """Back to ordinary Relay: the Bash router, the console alone. The terminal itself is untouched."""
        previous = self._active.pop((project_key(workspace), tab), None)
        state = ActivationState(str(Path(workspace).expanduser().resolve()), tab)
        if previous is not None:
            state.notes.append(f"deactivated {previous.plugin_id}")
        return state

    def active(self, workspace: str | Path, tab: str | None = None) -> ActivationState:
        state = self._active.get((project_key(workspace), tab))
        return state if state is not None else ActivationState(str(Path(workspace).expanduser().resolve()), tab)

    # selection
    def select_for(self, path: str | Path | None = None, foreground_program=None, *,
                   workspace: str | Path | None = None) -> list[Candidate]:
        """Plugins whose activation rules select this file and/or foreground program, with reasons.

        Without `workspace`, a file's own folder stands in, so a file opened from a project sees that
        project's packages. Enabled candidates come first, then by origin precedence. Disabled ones
        are listed too, so the UI can offer to enable them; nothing is activated here.
        """
        if workspace is None and path is not None:
            workspace = Path(path).expanduser().resolve().parent
        found = self.discover(workspace)
        state = _read_state(self.state_file)
        project = project_key(workspace) if workspace is not None else ""
        candidates = []
        for record in found.plugins.values():
            reasons = match_activation(record.manifest, path, foreground_program, project or None)
            if not reasons:
                continue
            ability = enablement(record, project, state)
            missing = tuple(d.program for d in dependency_status(record.manifest, which=self.which)
                            if d.missing and not d.optional)
            candidates.append(Candidate(record.id, record.origin, "; ".join(reasons), ability.enabled,
                                        ability.reason, missing))
        candidates.sort(key=lambda c: (not c.enabled, bool(c.missing), ORIGINS.index(c.origin), c.plugin_id))
        return candidates


# --- CLI -----------------------------------------------------------------------------------------

def _print_rows(rows: list[dict]) -> None:
    if not rows:
        print("no task plugins found")
        return
    for row in rows:
        mark = "on " if row.get("enabled") else "off"
        head = f"{mark} {row['id']:<18} {row['origin']:<8}"
        if not row["valid"]:
            print(f"{head} INVALID  {row['root']}")
            for issue in row["issues"]:
                print(f"      {issue['file']}: {issue['field'] or '(manifest)'}: {issue['message']}"
                      + (f" Fix: {issue['fix']}" if issue["fix"] else ""))
            continue
        print(f"{head} {row['version']:<8} {row['router']['language']:<6} {row['name']}")
        print(f"      {row['enable_reason']}")
        if row["shadowed_by"]:
            print(f"      shadowed by {row['shadowed_by']['origin']}: {row['shadowed_by']['root']}")
        for s in row["shadows"]:
            print(f"      shadows {s['origin']}: {s['root']}")
        for dep in row["dependencies"]:
            if dep["missing"]:
                print(f"      missing{' (optional)' if dep['optional'] else ''}: {dep['program']}"
                      + (f" — {dep['install_hint']}" if dep["install_hint"] else ""))
            elif dep["version"]:
                print(f"      {dep['found']}: {dep['version']}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="python3 -m relay_core.task_plugins",
                                     description="Inspect Relay task plugins (docs/TASK-PLUGINS.md).")
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("list", help="every package, its origin, enablement and dependencies")
    p.add_argument("--workspace", default=None)
    p.add_argument("--probe", action="store_true", help="run each found program's version argument")
    p.add_argument("--json", action="store_true")
    p = sub.add_parser("validate", help="validate one package folder")
    p.add_argument("package")
    for name in ("enable", "disable"):
        p = sub.add_parser(name, help=f"{name} a plugin for one project (recorded in {state_path()})")
        p.add_argument("plugin_id")
        p.add_argument("--workspace", default=".")
    p = sub.add_parser("select", help="which plugins a file or foreground program selects")
    p.add_argument("--path")
    p.add_argument("--program")
    p.add_argument("--workspace", default=None)
    p.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    registry = PluginRegistry()
    try:
        if args.command == "list":
            rows = registry.list(args.workspace, probe=args.probe)
            if args.json:
                print(json.dumps(rows, indent=2))
            else:
                _print_rows(rows)
        elif args.command == "validate":
            manifest = load_manifest(args.package)
            print(f"ok {manifest.id} {manifest.version} ({manifest.path})")
        elif args.command in ("enable", "disable"):
            ability = getattr(registry, args.command)(args.workspace, args.plugin_id)
            print(f"{args.plugin_id}: {'enabled' if ability.enabled else 'disabled'} — {ability.reason}")
        elif args.command == "select":
            found = registry.select_for(args.path, args.program, workspace=args.workspace)
            if args.json:
                print(json.dumps([c.to_dict() for c in found], indent=2))
            elif not found:
                print("no task plugin selects that")
            for c in [] if args.json else found:
                print(f"{'on ' if c.enabled else 'off'} {c.plugin_id} ({c.origin}): {c.reason}"
                      + ("" if c.enabled else f" — {c.enable_reason}")
                      + (f" — missing {', '.join(c.missing)}" if c.missing else ""))
    except ManifestError as exc:
        print(exc, file=sys.stderr)
        return 1
    except PluginError as exc:
        print(exc, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
