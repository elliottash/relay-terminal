---
id: G8DK
type: work
status: needs-qa-llm
labels: [feature]
component: [gui, worker]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (1M context), via Claude Code, 2026-09-17
rank: 1q
created: '2026-09-17'
acceptance: a saved command or prompt with parameters can be defined globally or per project, found in the palette and run by name
source: '`issues/feature_intake.txt`, 2026-09-17: "add a good version of aliased terminal commands / prompts that can be added globally or locally (sort of like warp \"workflows\")"'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-17-aliases-and-workflows/], related: [], github: null}
---
# Aliased terminal commands and prompts (Warp workflows)

## Notes
Owner decision (TASKS-AND-MEMORY-DESIGN.md section 9): global aliases live in the global Switchboard, local ones in the repo Switchboard.

## Open questions
1. One format for both commands and prompts (Markdown with front matter, `{{arg}}` parameters with defaults)?
2. Invocation: palette, `/name` for prompts, and a name typed in terminal mode?
3. Import existing Warp workflows and shell aliases?
4. Can the agent create aliases from repeated commands (proposed, logged)?

## Decisions (owner, 2026-09-17)
All recommendations accepted: one Markdown file per alias (command or prompt) with `{{arg}}` parameters and defaults,
global in the global Switchboard and local in the repo Switchboard; run from the palette, `/name` and the name in
terminal mode, filling parameters in the composer with Tab; import Warp workflows and shell aliases with a preview;
the agent may suggest aliases for repeated commands (suggestion only, logged).

## Implemented (2026-09-17)

Protocol: `docs/AGENT-SESSIONS-PROTOCOL.md` section 20. Architecture: `docs/ARCHITECTURE.md`,
section 11, "Aliases: saved commands and prompts".

### The file

One Switchboard card per alias, new card type `alias` (`backend/relay_core/board.py`: `CARD_TYPES`,
`ALIAS_STATUS_FOLDER`, `ALIAS_FIELDS`, `ALIAS_FOLDER`, `Card.expected_folder`), so
`scripts/relay-board.py check` validates them like any other card and no parallel format was
invented. Front matter carries `name`, `kind` (`command` | `prompt`) and `shell`; the **body**
carries the runnable text (`## Run` — the first fenced block, or the section itself for a prompt)
and the parameter defaults (`## Parameters` — `` - `name` = `default` — description ``).

Defaults are in the body on purpose: front matter scalars are single-line (format section 2.1) and
a default may hold commas, braces, quotes or backticks, which the YAML flow subset cannot carry.
A test covers exactly that (`a, b {c} \`d\` 'e'` round-trips).

### Where they live

| Scope | Root |
|---|---|
| global | `$XDG_CONFIG_HOME/relay/switchboard/aliases/` (owner decision, TASKS-AND-MEMORY-DESIGN section 9; override `RELAY_GLOBAL_SWITCHBOARD`) |
| local | `<repo>/issues/aliases/` with a Switchboard, else `<repo>/.relay/aliases/` |

A local alias hides a global one of the same name; the hidden one stays in the list marked
`shadowed`, so the UI can say so.

### Running one

Three paths, one message. `alias_run {name, values}` → `alias_expanded {text, kind, …}`, so the
substitution — and the quoting that goes with it — happens once, in the worker.

1. **Palette** — an "Aliases…" submenu, one row per alias, plus "Save the prompt box as an alias…"
   and "Import Warp workflows and shell aliases…". Opening the palette re-reads the list, because
   aliases are files somebody else may have written.
2. **`/name args`** — matched after the built-in slash commands, so an alias can never shadow one.
   The `/` popup lists aliases under the built-ins.
3. **The name typed in terminal mode** — only in terminal mode, only when the first word is exactly
   an alias name, and never after `!`, `*`, `/`, `.`, `~`, `#`, or with `=` in the first word.

A template whose parameters all have values runs straight away; one with a blank lands in the
prompt box with the blank selected, **Tab**/**Shift+Tab** between the fields (claimed before path
completion and before `agent.planToggle`, only while a template is live). Submitting sends the
field values, not the raw line. A line edited past recognition stops being an alias and goes to the
router as itself (`relay::aliases::reparse`).

### Import

`alias_import_preview` reads and never runs: Warp's workflows out of a **copy** of
`$XDG_STATE_HOME/warp-terminal/warp.sqlite` opened read-only, Warp workflow YAML under
`~/.warp/workflows/` and `<repo>/.warp/workflows/`, and shell aliases out of `.bashrc`,
`.bash_aliases`, `.bash_profile`, `.zshrc`, `.zshenv`, `.profile` and `config.fish` — parsed
textually, with a proper shell-word unquoter so bash's own `'\''` escape comes out right. A Warp
`agent_mode` workflow carries a `query`: that is a saved prompt and imports as `kind: prompt`.

The dialog shows the exact text that would be stored, the origin, and any warning (replaces an
existing alias, the name is also a program on `PATH`, the text contains `sudo` / `rm -rf` /
`curl` / `wget` / a pipe into a shell, the name was shortened). **A row carrying a warning starts
unticked.** `alias_import_apply` names rows from the preview the worker is holding: the caller may
rename one, but cannot supply text, so an import can only store bytes the worker read and showed.

### The agent's suggestion

`suggest {kind: "alias", commands}`. `aliases.repeats()` is a pure rule (three runs or more) and it
runs **before** any model call, so nothing repeated means no call. One cheap `suggestions`-role
side call proposes a name, a title and the placeholders; the reply is validated into a real alias
and a rejected reply comes back empty. Logged to `worker.log` (`alias suggestion requested`,
`alias suggested`); nothing is written until the user saves it, and a saved suggestion carries
`source: 'agent suggestion, <date>'`.

### Shortcut hints (WARP.md standing rule)

Running an alias from the palette is the slow path: it hints `Next time: type /<name>`, and for a
command `, or just <name> in terminal mode`. Hint id `alias.run.<name>`, per-id limit as usual.
`relay::aliases::fastPathHint` and a test for it; the palette's own generic hint still covers
"Save the prompt box as an alias…" and the import action.

### Files

| File | Why |
|---|---|
| `backend/relay_core/aliases.py` | new: the format, the store, global/local resolution, quote-aware substitution, repeated-command detection |
| `backend/relay_core/alias_import.py` | new: the Warp (sqlite + YAML) and shell-alias importers, the preview and the apply |
| `backend/relay_core/board.py` | the `alias` card type, its folder, fields and statuses; a public `atomic_write` |
| `backend/relay_core/session_protocol.py` | the six section-20 handlers and `suggest kind: "alias"` |
| `backend/relay_core/suggestions.py` | `propose_alias`: the side call and its validation |
| `remote/wire.py` | classifies all six new events as withheld, with reasons |
| `src/Aliases.h`, `src/Aliases.cpp` | new `relay-aliases`: composer fields, Tab, reparse, invocation matching, palette text, the hint |
| `src/main.cpp` | the Pane alias block, the three invocation hooks, Tab, the event handler, the palette rows and the import dialog |
| `CMakeLists.txt` | `relay-aliases` and its `aliases` ctest target |
| `tests/test_aliases.py` | new: substitution and defaults, the format, global vs local, repeats |
| `tests/test_alias_import.py` | new: both importers, malformed input, the preview and the apply |
| `tests/test_alias_protocol.py` | new: the three invocation paths, save/delete, import, the suggestion |
| `tests/aliases_test.cpp` | new: the GUI-side rules |
| `docs/AGENT-SESSIONS-PROTOCOL.md` | section 20 (appended; 19 went to the Switchboard branch) |
| `docs/ARCHITECTURE.md` | the aliases subsection, the source map, the hint-trigger list |

## Acceptance evidence

`docs/qa_evidence/2026-09-17-aliases-and-workflows/`, all `implementer-` prefixed.

- `implementer-driver.sh` — the Xvfb run, isolated `HOME`/`XDG_CONFIG_HOME`/`XDG_DATA_HOME`/
  `XDG_STATE_HOME`, `RELAY_KEYRING=off`.
- `01`–`06` defining an alias from the prompt box; `implementer-alias-card.txt` is the card that
  landed in the repository Switchboard.
- `07`–`12` running it from the palette: the submenu, the fields in the composer, typing the first
  field, **Tab** to the second, and the command running.
- `13`–`15` `/rec` in the slash popup, `/recent 3 docs`, and the run.
- `16`–`17` `recent 2 backend` typed in terminal mode, and the run.
- `18` a parameter value of `a;touch …`: the terminal shows `git log --oneline -1 -- 'a;touch'` —
  one word. `implementer-injection-check.txt`: "OK: the injected command never ran".
- `19`–`20` `/explain`, a **prompt** alias from the **global** Switchboard, written after the pane
  started and found because the list is re-read.
- `21`–`24` the import preview and the import. The preview is against **this machine's real Warp
  workflows** (the database is copied into the jail): 7 rows including the `agent_mode` one
  correctly shown as `prompt`, plus the jail's shell aliases. `eth-vpn`, `update-system` and `gs`
  start unticked with their warnings shown; `ll` and the deliberately unbalanced `broken=` line are
  skipped with reasons. `implementer-after-import.txt` lists what was written.
- `implementer-suggestion-live.py` / `.txt` — one **live** model call (the stored `glm-coding` key,
  read through `keystore.lookup`, never printed or written): repeats found offline, then a proposed
  alias `pytest {{test_file}} -k {{keyword}} -x` with both defaults, and what it would run with a
  hostile value in every parameter (`pytest 'v; rm -rf / #' -k 'v; rm -rf / #' -x`). Nothing written.
- `implementer-relay.log`, `implementer-worker.log`, `implementer-relay-stderr.log`.

Tests: `./scripts/test.sh` 725 tests OK; `ctest --test-dir build` 19/19 OK (new target `aliases`,
25 cases). One test asks a real interactive bash whether each substituted value is one literal
word, so the quoting rules are checked against bash rather than only against our reading of them.

## QA checklist

1. **Define one.** Type a command with `{{two}} {{blanks}}` in the prompt box, open the palette and
   choose "Save the prompt box as an alias…". Name it. The file must appear at
   `<repo>/issues/aliases/<name>.md` (or `<repo>/.relay/aliases/<name>.md` without a board) as a
   card with `type: alias`, and `scripts/relay-board.py check` must pass on it.
2. **The three paths agree.** Run it from the palette submenu, then as `/name a b`, then by typing
   `name a b` in terminal mode. All three must put the *same* line on the prompt. The palette run
   — and only the palette run — must show a hint naming `/name` (and the typed name, for a command).
3. **Fields and Tab.** From the palette, an alias with a blank parameter must land in the prompt box
   with the blank *selected*. Tab moves to the next field and selects it, Shift+Tab back, and both
   wrap. While a template is live Tab must not do path completion; the moment you rewrite the
   command itself (change a literal part), Tab goes back to completion and Enter routes the line as
   an ordinary command.
4. **Defaults.** An alias whose every parameter has a default must run straight from the palette
   with no editing. One with a parameter that has no default must not run until it is filled.
5. **Quoting.** Run an alias with a value of `a; touch /tmp/pwned`, then `$(touch /tmp/pwned)`, then
   `` `touch /tmp/pwned` ``, then `x" ; touch /tmp/pwned ; "`. `/tmp/pwned` must never exist, and
   the terminal must show the value as one quoted word. Repeat with the placeholder written as
   `"{{p}}"` and as `'{{p}}'` in the alias file.
6. **Global vs local.** Put an alias of the same name in both Switchboards with different text.
   `/name` must run the local one; the palette must show the global one marked as hidden, or not at
   all; deleting the local one must make the global one live again. With no project open, a global
   alias must still run.
7. **Prompt aliases.** A `kind: prompt` alias must go to the **agent**, not the shell, and typing
   its name in terminal mode must *not* run it (it is a shell word there). `/name` must.
8. **A built-in is never shadowed.** Create an alias called `model`. `/model` must still open the
   model picker, and the `/` popup must list the built-in first.
9. **Import.** Palette → "Import Warp workflows and shell aliases…". Nothing may run: put
   `alias probe='touch /tmp/probe'` in a startup file and check `/tmp/probe` never appears, before
   or after the preview. Every row must show the exact text; rows with a warning (`sudo`, `rm -rf`,
   a name on `PATH`, replacing an existing alias) must start unticked. Import a subset and check
   only those were written, in the scope chosen. Cancel and check nothing was written.
10. **Malformed input.** Add an unbalanced `alias bad='never closed`, a line continued with `\`, and
    a `.warp/workflows/x.yaml` that is not a workflow. Each must appear in the skipped list with a
    reason; the good ones beside them must still import. Point Relay at a `warp.sqlite` that is not
    a database: a reason, not a crash. Check `warp.sqlite`'s mtime is unchanged after a preview.
11. **A broken alias file.** Put a card with no `## Run` section, and one with unparseable front
    matter, in `issues/aliases/`. The list must still show the others, and the palette must not be
    empty; the bad ones come back as `problems`.
12. **The agent's suggestion.** Run the same non-trivial command three times, then ask for a
    suggestion. It must be a suggestion — nothing written until you save it — and `worker.log` must
    carry `alias suggestion requested` and `alias suggested`. Two runs must produce no model call at
    all. A saved suggestion must carry `source: agent suggestion, <date>`.
13. **Privacy.** Nothing new leaves the machine except the repeated commands the suggestion call
    sends to the pane's own provider. `remote/wire.py`: no alias event may be forwarded to a phone.
    Check the logs for any alias body or startup-file content (there should be none).

## Known gaps

- **No alias manager UI.** Aliases are created from the prompt box or imported, and edited by
  opening the card file. There is no list-with-edit dialog, and `alias_delete` has no button — only
  the message. The palette submenu is the only browsing surface.
- **The title of an alias saved from the prompt box is its own text**, truncated, because there is
  nowhere in the one-field dialog to ask for one. Editing the card's `# ` heading fixes it.
- **`## Parameters` is only read, never written back from the UI.** An alias saved from the prompt
  box declares its parameter *names* with no defaults and no descriptions; you add those by editing
  the file. The importers do write defaults, because Warp supplies them.
- **`reparse` takes the first match of each literal**, so an alias whose literal separator can also
  appear inside a value (`echo {{a}} {{b}}` with a value containing a space) splits at the first
  space. Filling from the palette is unaffected; only re-reading a hand-edited line is.
- **Positional arguments are split like shell words**, so `/name "two words"` fills one parameter
  but `/name two words` fills two. There is no `--param=value` form.
- **An alias can shadow a program on `PATH`** when typed in terminal mode. The import preview warns,
  and nothing warns for an alias created by hand.
- **The global Switchboard is only aliases so far.** `$XDG_CONFIG_HOME/relay/switchboard/` gets an
  `aliases/` folder and no `board.yaml`, so `relay-board.py` does not know about it; global memory
  and global skills (the rest of section 9) are not built.
- **Warp workflow YAML is read by a purpose-built parser**, not a YAML library, because the backend
  is stdlib-only. It handles the shape Warp writes; anything else is skipped with a reason rather
  than half-read. No `.yaml` workflows existed on the development machine to test against — only
  fixtures — while the sqlite path was tested against the real database.
- **Aliases are not offered to the agent as a tool.** The agent can suggest one; it cannot list or
  run them.
- **No `alias_changed` watch.** The list is re-read when the palette opens and when `/` is typed, so
  a card written by an agent mid-turn appears at the next of those, not immediately.
