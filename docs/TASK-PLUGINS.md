# Task plugins

A **task plugin** is a workspace kind a Relay tab can be switched into (#MEPR, #C0Q8). Relay has
one kind built in, the Bash pane, whose composer routes each line to the shell or the agent. A
plugin declares the same pieces for another task — a TeX document, a Python or Stata analysis —
in one versioned manifest:

| Piece | What it changes |
|---|---|
| `router` | Which language the composer checks a line against, and the chip it shows (`TEX`, `PY`, `STATA`) |
| `runner` | What runs a runnable line or builds the document: a kernel, an artifact command or a REPL |
| `tools` | A lazy tool group offered to the pane's agent and to a guest agent in that pane |
| `skills` | SKILL.md folders the agent can load while the workspace is active |
| `panes` | The roles the linked pane group fills and its layout presets |
| `preview` | The adapter that shows the output, and which files are output |
| `requires` | The local programs it needs, checked before activation |

A task plugin is not an MCP integration, and Relay never treats a Claude Code or Codex
`plugin.json` as one: those describe agent extensions for another host. Skills from those tools
are read through the ordinary skill sources (`docs/ARCHITECTURE.md`, Skills).

The contract and the lifecycle are `backend/relay_core/task_plugins.py` (tests:
`tests/test_task_plugins.py`); neither starts a process. What an *active* workspace does — its
router, its runtime and its tools — is `backend/relay_core/workspace_plugins.py` (tests:
`tests/test_workspace_plugins.py`), on the wire as protocol section 36
(`docs/AGENT-SESSIONS-PROTOCOL.md`). The panes that show a workspace are #E85D's.

## Where packages live

Each package is a folder holding `plugin.json`. For one `id`, the first origin wins and the others
are listed as **shadowed**, with the path of the package that shadows them:

| Origin | Folder | Enabled by default |
|---|---|---|
| `project` | `<workspace>/.relay/plugins/<pkg>/`, and the same folder in each parent up to the git root (nearest first) | Only if it declares no `runner` and no `tools` |
| `global` | `$XDG_CONFIG_HOME/relay/plugins/<pkg>/` (`~/.config/relay/plugins`) | Yes — you put it there |
| `bundled` | `backend/relay_core/plugins_bundled/<pkg>/`, installed with the backend | Yes |

A package folder that is a symlink out of its plugins folder, a manifest that is a symlink, and a
package holding a file that links outside itself are refused. The `relay.` id namespace belongs
to the bundled plugins: another origin may use a `relay.` id only to override a bundled plugin of
that id.

Relay ships three: `relay.tex` (latexmk → PDF, `tex_*` tools), `relay.python` (a kernel shared
by the composer and the agent, `py_*` tools, a Variables pane) and `relay.stata` (the Stata
console as a REPL until the bridge is chosen — #MEPR decision 2; its two declared tools are
reported as unavailable until then).

## Why JSON

#MEPR sketched `plugin.yaml`. The manifest is JSON instead, for three reasons: Relay's backend
supports Python 3.10, which has no TOML reader in the standard library and no YAML reader at all
(PyYAML would be a new dependency of the worker); the C++ side can read the same file with
`QJsonDocument` when it needs a layout or a preview adapter, without asking the worker; and JSON
has one way to write each value, so validation errors point at exactly one thing. The cost is no
comments; `description` fields carry the explanation instead.

## The manifest, schema version 1

```json
{
  "schema_version": 1,
  "id": "relay.tex",
  "version": "0.1.0",
  "name": "TeX document",
  "description": "Edit a LaTeX source, build it with latexmk, and preview the PDF.",
  "activation": {"files": ["*.tex", "*.ltx"], "programs": []},
  "router": {"language": "tex", "prefixes": []},
  "runner": {
    "kind": "artifact_command",
    "command": ["latexmk", "-pdf", "-interaction=nonstopmode", "-synctex=1", "{file}"],
    "cwd": "file_dir",
    "env": ["TEXINPUTS"]
  },
  "tools": {"group": "tex", "items": [{"name": "tex_build", "description": "Build the PDF."}]},
  "skills": ["skills/tex-workspace"],
  "panes": {"roles": ["editor", "console", "preview"], "layouts": ["1:1:1", "2:1"], "default_layout": "1:1:1"},
  "preview": {"adapter": "pdf", "outputs": ["*.pdf"]},
  "requires": [{"program": "latexmk", "version_arg": "-v", "install_hint": "install TeX Live"}]
}
```

| Field | Rules |
|---|---|
| `schema_version` | Required, the integer `1`. Any other value is refused outright — its keys may mean something this Relay cannot check. |
| `id` | 2–4 lowercase dotted segments (`yourname.sql`), at most 64 characters. |
| `version` | Semantic version, `MAJOR.MINOR.PATCH[-pre]`. |
| `name`, `description` | Non-empty; at most 80 and 400 characters. |
| `activation.files` | Workspace-relative globs. Without `/` a glob matches the file name (`*.tex`); with `/` it matches the path from the project root (`chapters/*.tex`). |
| `activation.programs` | Basename patterns for the pane's foreground program (`python3*`, `ipython`); `.exe` is ignored. |
| `router.language` | `bash`, `python`, `stata` or `tex`. Default `bash`. `tex` routes like Bash — the document console is a shell — and marks the tab as a document workspace. |
| `router.prefixes` | Extra prefixes that force the language route (IPython's `%`). `!` (shell), `*` (agent) and `/` (Relay commands) are reserved in every workspace and cannot be declared. |
| `runner.kind` | `kernel` (a persistent process speaking the Jupyter protocol), `artifact_command` (source → output file plus diagnostics) or `repl` (an interactive program in the pty). |
| `runner.command` | An **argv list**, never a shell string. `argv[0]` is a program name listed in `requires` (the launcher uses whichever of that entry's alternatives was found) or a `./path` inside the package. Absolute paths, `env`, and a shell given a command string (`sh -c`, `cmd /c`, `pwsh -Command`) are refused. Placeholders: `{file}`, `{file_stem}`, `{file_dir}`, `{workspace}`, and `{connection_file}` for kernels; `{{` and `}}` are literal braces. |
| `runner.cwd` | `workspace` (default), `project_root` or `file_dir`. |
| `runner.env` | Names passed through from Relay's environment; nothing else is. A name that looks like a credential — one of its `_`-separated parts is `KEY`, `TOKEN`, `SECRET`, `PASSWORD`, `AUTH`, `COOKIE`, `SESSION`, `PRIVATE` or similar, as in `OPENAI_API_KEY` or `SSH_AUTH_SOCK` — is refused. |
| `tools.group` | 1–16 lowercase letters/digits; not one of Relay's own groups or native tool prefixes (`board`, `app`, `run`, `read`, …). |
| `tools.items` | `{name, description}`; each name is `<group>_<verb>`, at most 64 characters, so it is a valid tool name for every provider. |
| `tools.lazy` | Optional and only `true`: v1 groups are always fetched on demand through `load_tools`. |
| (tool code) | A manifest *declares* its tools; Relay offers only the declared names it has code for (`workspace_plugins.RUNTIMES`, keyed by plugin id). A third-party package cannot ship tool code in v1, so its group is listed as unavailable. |
| `skills` | Package-relative folders, each holding `SKILL.md`. |
| `panes.roles` | Some of `editor`, `console`, `preview`, `variables`; `console` is required, because the tab's terminal stays in every workspace. |
| `panes.layouts` | Column weights joined by `:` (`1:1:1`, `2:1`), at most one column per role. `default_layout` must be one of them. |
| `preview.adapter` | `pdf`, `image`, `svg`, `html` or `table`; needs a `preview` pane. `outputs` are workspace-relative globs. |
| `requires[]` | `program` (a PATH name), optional `alternatives`, `version_arg` (default `--version`; `[]` means "never probe"), `optional`, `install_hint`. |

Paths anywhere in a manifest are relative and written with `/`: an absolute path, a `~`, a drive
letter, a `\` or a `..` component is refused, and a path that resolves outside the package through
a symlink is refused too. Unknown keys are errors, with the nearest known key suggested — a
misspelt `requires` must not silently drop a dependency check.

Validation reports every problem in one pass, each as `file: field: message Fix: what to do`:

```
.relay/plugins/sql/plugin.json: runner.command: is a shell string; Relay never runs a command through a shell. Fix: write it as an argv list: ["duckdb", "-c", "select 1"].
.relay/plugins/sql/plugin.json: requries: unknown key. Fix: did you mean 'requires'?
```

## Lifecycle: discovered → enabled → active

**Discovered.** Listing reads files and resolves `requires` with `shutil.which`. It never runs a
program; `--probe` (or `dependency_status(probe=True)`) runs each found program's `version_arg`
only when asked, with a minimal environment and a 5-second timeout.

**Enabled.** Opening a cloned project must not launch its command, kernel or tools. So a project
package that declares a `runner` or `tools` stays disabled until someone enables it, and that
decision is recorded in Relay's own config, `$XDG_CONFIG_HOME/relay/plugins.json`
(`RELAY_PLUGIN_STATE` overrides the path) — never in the repository:

```json
{"version": 1, "projects": {"/home/me/paper": {"acme.sql": {"project": {
  "enabled": true, "digest": "sha256:…", "at": "2026-09-23T10:00:00+0200", "version": "0.1.0", "root": "…"}}}}}
```

- The key is the project's resolved git root, so a copy of the repository somewhere else is a
  different project, and a state file shipped inside the repository is never read.
- A project package's enable is pinned to a digest of every file in the package. When a pull
  changes any of them, the package reports "changed since it was enabled" and will not activate
  until it is enabled again.
- Bundled and global packages can be disabled for one project the same way; disabling one
  deactivates it in that project's tabs.
- Writes are read-modify-write under a lock beside the file and replaced atomically.

**Active.** `PluginRegistry.activate(workspace, plugin_id, tab=None)` returns the
`ActivationState` a tab adopts: the router to show before anything runs, the runner it would start
(not started here), the tool group, the skill folders, panes and preview, and dependency status.
It refuses a plugin that is not enabled (the error names the command that enables it) or whose
required program is missing (naming the `install_hint`). Activating another plugin in the same
tab replaces the first. `deactivate(workspace, tab)` returns the default state — the Bash router,
the console alone, no runner — and leaves the terminal itself alone.

**Selection.** `select_for(path=None, foreground_program=None, workspace=None)` lists the plugins
whose activation rules match a file or the foreground program, each with its reason
(`file 'paper.tex' matches activation.files '*.tex'`), enabled ones first. Disabled candidates are
listed too, with why, so the UI can offer to enable them. Nothing is activated by selection:
forcing Bash (`!`) or the agent (`*`) stays available in every workspace.

## Command line

```
PYTHONPATH=backend python3 -m relay_core.task_plugins list [--workspace DIR] [--probe] [--json]
PYTHONPATH=backend python3 -m relay_core.task_plugins validate path/to/package
PYTHONPATH=backend python3 -m relay_core.task_plugins enable|disable ID --workspace DIR
PYTHONPATH=backend python3 -m relay_core.task_plugins select [--path FILE] [--program ARGV0] [--workspace DIR] [--json]
```

`validate` exits 1 on manifest errors; `enable`, `disable` and the others exit 2 on a lifecycle
error such as an unknown id.

## Writing a project plugin

```
.relay/plugins/sql/
  plugin.json
  skills/sql-workspace/SKILL.md
```

Run `validate` on it, then `list --workspace .` to see it, its origin and what it is missing. If
it declares a runner or tools, `enable` it for this project after reading what it runs.

## What an active workspace does

`WorkspaceManager` (`workspace_plugins.py`) holds a worker's active workspaces, keyed by a
workspace id (the pane's own is `pane`), and each owns its runtime:

- **Router.** In a `python` or `stata` workspace the composer's line is checked by
  `lang_router.classify_line` instead of `bash -n`; `!` (shell), `*` (agent), `/shell`, `/agent`
  and the natural-language rules keep working, `!!`/`**` hand the character to the program, and
  the decision's `language` and `target` reach the GUI before submit. A Python/IPython/Stata REPL
  in the foreground routes the same way with no plugin active. A `tex` workspace routes like Bash.
- **Runner.** `relay.python` gets one `py_kernel.KernelSession`: ipykernel when this Relay's
  Python has `jupyter_client` and a kernel spec, otherwise the stdlib server under the user's
  `python3` — so ipykernel is optional. The composer's `kernel_run` and the agent's `py_run_cell`
  share it. `relay.tex` gets one `tex_build.TexBuilder` on the document's detected root. The
  runtime's environment is Relay's minimal set plus the manifest's `runner.env`.
- **Tools.** The group is loaded with `load_tools` and exists only while that workspace has the
  plugin active, for the native agent and for a Claude Code or Codex guest alike.
- **Cleanup.** Deactivating, replacing the plugin, or the worker shutting down closes the
  runtime. Nothing is shared between two workspace ids.

## Schema version 2

Version 2 adds console-kind declarations to the version 1 workspace manifest. Version 1 remains valid; its unknown-key checks still reject version 2 fields. A manifest must declare `schema_version: 2` to use the fields below. `validate` checks their types, allowed values, paths and command argv. `describe ID [--workspace DIR]` prints the resolved manifest as JSON, including all three surfaces, without starting its program.

| Field | Meaning |
| --- | --- |
| `console.program` | Executable argv. The first element must be a program in `requires` or a `./` package file. `{workspace}`, `{file}` and `{connection_file}` placeholders are accepted. The consumer resolves package-relative startup files before launch. |
| `console.env` | Names of non-secret environment variables to pass. |
| `console.prompt_marks` | `osc133` or `none`. OSC 133 uses `A` before the prompt, `B` before typed input, `C` before output and `D;<status>` after execution. |
| `console.startup` | Existing package-relative file; absolute paths, `..` and escaping symlinks are refused. |
| `completion.kind` | `shell` for program completion or `static` for a bundled table. |
| `completion.table` | Package-relative JSON list of `{text, description}` objects, required for `static`. |
| `commands` | Visible slash actions with `name`, `description`, optional `args` (`name`, `required`), `action` and optional `when` (`roles`, `languages`). Actions are `tool`, `prompt` or `program_line`, carrying `tool`, `prompt` or `line` respectively. Declaring a slash action does not grant a tool. |
| `router.prose_fallback` | `agent` or `program` for lines the router cannot classify. |

The bundled `relay.shell` package describes the default Bash console and includes the same OSC 7/133 integration script used by a shell pane. `relay.python` declares a Jupyter console, IPython startup marks, a static completion table and `/restart`, `/vars`, `/export`; `relay.tex` declares `/build` and `/errors`. These are manifest surfaces for console consumers; the existing workspace runtime still uses its `runner` field.
