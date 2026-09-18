---
id: E4TX
type: work
status: needs-qa-llm
labels: [feature]
component: [agent]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus (Claude Code, subagent), 2026-09-18
rank: c
created: '2026-09-18'
acceptance: 'A model changing an existing file calls `edit_file {path, old_string, new_string, replace_all?}` instead of resending the whole file; the preview is a `EDIT FILE` diff in the same shape as `WRITE FILE`; every refusal says what to do instead; the edit is undone by `rewind` and refused in plan mode; `tests/test_tools.py`, `tests/test_sessions.py`, `tests/test_subagents.py`, `tests/test_agents_defs.py`, evidence in `docs/qa_evidence/2026-09-18-edit-file-tool/`'
source: 'owner, 2026-09-18: a real edit tool "as part of this" tool-call-notifications work'
links: {plans: [], commits: [d3f5624], evidence: ['docs/qa_evidence/2026-09-18-edit-file-tool/'], related: [], github: null}
---
# edit_file: a change to an existing file is one string, not the whole file

## Issue

owner, 2026-09-18: a real edit tool "as part of this" tool-call-notifications work.

Until now `write_file {path, content}` was the only way to change a file, so every edit resent the
whole thing — slow, expensive, and an invitation to drop the parts of a file the model was not
thinking about. `docs/OPENCODE-NOTES.md` had already flagged it, and `docs/AGENT-FEATURES-RESEARCH.md`
called the tool `edit_file` before it existed.

## Change

**The tool.** `edit_file {path, old_string, new_string, replace_all?: bool}` replaces an exact
string in one existing UTF-8 workspace file. It runs under exactly the guards `write_file` has
(`Workspace.resolve` with the symlink and secret-file guards, regular files of at most 128 KiB, the
file's SHA-256 rechecked between the preview and the write, an atomic temp-file + `os.replace` that
keeps the file's mode, the cancel check), and shares the write itself: `prepare` computes the whole
new text from `old_string`/`new_string`, so the diff the user reviews is the bytes that land on
disk, and `execute` is the same code path `write_file` uses.

**Refusals say what to do instead** (all `ValueError`, before anything is written):

| Situation | Message |
|---|---|
| The file does not exist | `edit_file needs a file that already exists; use write_file to create one.` |
| `old_string` empty | `old_string must not be empty. Use write_file to create a file or replace one in full.` |
| `old_string` == `new_string` | `old_string and new_string are identical; the edit would change nothing.` |
| Not found | `old_string was not found in the file. Read the file again and copy the exact text, including whitespace and indentation.` |
| Found N > 1 without `replace_all` | `old_string occurs N times in the file. Add surrounding lines so it matches once, or set replace_all: true to change all N.` |
| The file changed since the preview | the existing `File changed while the write was prepared…` |

**The preview** keeps the `WRITE FILE` shape so the GUI needs no new parser: first line `EDIT FILE`,
blank, the absolute path, blank, the unified diff (same `difflib` call and `a/`…`b/` labels), blank,
`Old bytes: N; new bytes: M.` Mapping that title to a verb in `src/main.cpp` is a separate change,
by another session.

**Results** now carry what a "+3 −1" chip needs: `edit_file` →
`{path, written_bytes, sha256, replacements, added, removed}`; `write_file` →
`{path, written_bytes, sha256, added, removed, created}`. `added`/`removed` count the diff's `+`/`-`
lines.

**Everywhere `write_file` was special-cased, `edit_file` is too**: the turn's checkpoints (so
`rewind` undoes an edit exactly like a write), `PLAN_BLOCKED_TOOLS`, `SUBAGENT_TOOLS`, the
read-only discard and the read-only wording in a subagent's prompt, and the `allowed` map in
`ToolExecutor.prepare`. Foreign agent definitions map their edit tools onto it: Claude Code's
`Edit`/`MultiEdit` → `edit_file`, opencode's `patch` and Gemini's `replace` → `edit_file` +
`write_file` (both can create a file), `Write` stays `write_file` alone; an opencode `permission`
short of `allow` for `edit`/`write`/`patch` withholds both tools.

**The prompts.** The `TOOLS` descriptions now say plainly which tool is for what (`write_file`: a
new file or a full rewrite; `edit_file`: preferred for changing a file you have read), and one
sentence of the system prompt says the same. Protocol: `docs/AGENT-SESSIONS-PROTOCOL.md` section 11
("File writes and their previews"), plus the checkpoint and plan-mode lines.

## QA checklist

1. **A real edit.** Ask the agent to change one line of a file it has read. It calls `edit_file`,
   the transcript shows an `EDIT FILE` diff of just that line, and the rest of the file is
   untouched (`git diff` shows one hunk).
2. **Ambiguity is reported, not guessed.** Ask for a change to a string that appears several times.
   The tool refuses with the count, and the model's next call either adds context or sets
   `replace_all` — nothing is written in between.
3. **replace_all.** A rename with `replace_all: true` changes every occurrence, and the result says
   `replacements` equal to the number of matches.
4. **A new file still works.** Ask for a file that does not exist: the model is told to use
   `write_file`, and does.
5. **Undo.** After an `edit_file` turn, `rewind` on that turn restores the file's pre-edit bytes,
   and the checkpoint list names the edited file.
6. **Plan mode.** In plan mode the tool list has neither `write_file` nor `edit_file`, and asking
   for an edit produces a plan instead.
7. **Guards.** An edit aimed at `.env`, `.git/config`, a symlink, or a path outside the workspace is
   refused with the usual guard message.
8. **Raced write.** Change the file by hand between the preview and the write (a long turn, or the
   scripted case in `tests/test_tools.py::test_refuse_stale_edit`): the edit is refused and nothing
   is overwritten.
9. **Cost.** Compare token use for a one-line change against the old behavior: the request should
   carry the edited string, not the file.

## Known gaps

- The GUI still prints `EDIT FILE` previews through the generic path: `src/main.cpp` (~5615) maps
  `WRITE FILE` to the verb "write" and has no row for `EDIT FILE` yet, and nothing shows the new
  `added`/`removed` counts. Both are another session's change, by design.
- `docs/ARCHITECTURE.md`'s tool table has no `edit_file` row: at the time of this commit another
  session had uncommitted work in exactly that table, and the two could not be separated. It should
  be added once that lands.
- `docs/VALIDATION.md` was not updated for the same reason: another session was rewriting both the
  QA-lane list and the `tests/test_tools.py` row while this landed. This card belongs on that list,
  and that row is six cases short.
- No multi-edit (several replacements in one call) and no line-number addressing: one exact string
  per call, like Claude Code's `Edit`.
