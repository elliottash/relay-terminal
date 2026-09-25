# Relay owns agent scratch

Card #DVV2. An agent never picks a scratch path. It asks Relay for one, says what it is for, and
Relay decides where it lives, records it in a ledger, and ends it. Anything that must outlive the
task is not scratch: it goes where the project keeps it, and Relay moves it there.

Two things this replaces: important work left in `/tmp` (wiped at reboot, invisible to anyone
else) and agent-made folders cluttering the top of `$HOME`. Both were real — 380 GB in
`/tmp/claude-<uid>` on 2026-09-24, plus `~/tmp`, `~/relay-phone`, `~/projects-reorg`… — and in
both cases nobody could tell what was still needed.

## Three classes, three homes

| class | home | lifetime | example |
|---|---|---|---|
| `scratch` | `$XDG_CACHE_HOME/relay/scratch/<session>/<slug>/` | task or session; reclaimed at close | exported tree, build dir, test workspace |
| `keep` | `<project>/.relay/work/<slug>/` (git-ignored via `/.relay/`) | until promoted into the repo (`--promote-to`), or explicitly dropped | evidence draft, report, message draft |
| `install` | `$XDG_DATA_HOME/relay/tools/<name>/` | until the user removes it | a tool an agent installs |

The cache dir rather than /tmp for `scratch`, because it survives a reboot mid-task, it is
per-user, and cleaners and backup tools already know to skip it. Each home honours an override —
`RELAY_SCRATCH_HOME`, `RELAY_TOOLS_HOME`, `RELAY_STATE_HOME` — over the XDG variable, over the
`$HOME` default. The same resolution exists in two places that must not drift:
`relay::scratchpaths` in `src/AppPaths.h` and `scratch_root()`/`tools_root()`/`ledger_path()`/
`keep_root()` in `backend/relay_core/scratch.py`. This file is the contract; change both together.

## The ledger

One append-only JSONL per user: `$XDG_STATE_HOME/relay/scratch-ledger.jsonl` (override
`RELAY_LEDGER`). Each record is a full row snapshot; a transition appends a new record with the
same id and reading takes the last one, so the file is a replayable history and a half-written
last line (a crash mid-append) costs nothing. Row schema:

```
id           scxxxx            unique, printed by `relay-scratch new`
path         /home/u/.cache/relay/scratch/<session>/<slug>
class        scratch | keep | install
purpose      one line, what it is for
created_by   {session, pane, model, card}
created_at   ISO timestamp
lifetime     task | session | days:N | until-promoted | user
state        live | released | promoted | reclaimed | orphaned
size         bytes at last scan          freed: bytes freed when reclaimed
```

`relay-scratch report` reads the ledger first and walks the directories only for what is on disk
but not in it; `relay-scratch ledger` prints totals by class and by session, unpromoted `keep`
rows, and orphans. The app's scratch monitor (#SZHQ) shows the same numbers.

## The API agents use

- The `scratch_dir` tool (agent sessions): `class`, `purpose` (required, one line), `card`,
  `lifetime`. Returns the path; the row is written with the session's identity.
- `scratch_release`: end a row — `scratch` is reclaimed, `keep` is promoted (`promote_to=`) or
  dropped (`drop=True`) explicitly, `install` stays.
- Shells and guests: `relay-scratch new --class scratch --purpose "…" [--card X]` prints the path;
  `relay-scratch release <id-or-path>`.
- `TMPDIR` for every process Relay spawns for a session — the backend's own `run_command`
  children, guest CLIs, and the pane's shell — points at `<scratch home>/<session>/tmp`, which is
  itself a ledger row. `mktemp`, `mktemp -d` and Python's `tempfile` therefore land somewhere
  owned and ledgered with zero agent effort.

## Enforcement, in escalating order

1. `TMPDIR` (above) puts the default somewhere owned.
2. The tool layer refuses a `write_file`/`edit_file` under the system temp dir or creating a new
   top-level `$HOME` entry, with the exact `scratch_dir`/`relay-scratch new` call to use instead;
   a deliverable (`.md` report, evidence, a message file) is refused outright outside the
   workspace even where Options allows the folder.
3. At turn end, a bounded top-level sweep of `/tmp` and `$HOME` lists entries created during the
   turn that are not in the ledger, and puts them to the agent — ledger it, move it, or delete
   it — before the turn closes. It asks; it never deletes.

## Cleanup is a ledger transition

`task` scratch goes when its task closes, `session` scratch when the session ends (`end_session`),
`days:N` at expiry, released rows when no live process uses them. Before anything is deleted the
#SZHQ safety rules hold (owned by this user, no symlink tricks, no live process using it, not
`land/`'s root, not the ccache), the row is marked `reclaimed` with the size freed, and
`gc --apply` refuses to run below a 6-hour idle floor on the default roots without `--force-idle`
(added after a `--idle-hours 0 --apply` deleted other sessions' 3–7 h scratch in one line — the
floor is load-bearing). `keep` rows are never auto-deleted; `install` rows stay until the user
removes them. Orphans (on disk, pre-dating the ledger) are listed by `relay-scratch adopt` for the
user to keep, promote or reclaim — never deleted automatically.

## Migration

`relay-scratch adopt` (dry run; `--apply` writes) records the pre-ledger backlog as `orphaned`
rows: the `/tmp/claude-<uid>` and `/tmp/relay-*`/`tmp*` entries the #SZHQ walk finds, and the
known agent-made `$HOME` folders, with the creator guessed from path and mtime. From that one list
the user keeps, promotes or reclaims each.
