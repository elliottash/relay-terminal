---
id: DVV2
type: work
status: needs-verification
labels: [feature, disk, agents, architecture]
assignee: agent
implemented_by: glm/glm-5.3
session: c4801fd5-8cf2-43b1-9c6e-122052fce8d1
waiting_on: owner
priority: 3
rank: zzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
source: Claude Code pane, 2026-09-24
links: {commits: [0da66145, 35fa497b, acdbb2d0, 275d4e27], evidence: [docs/qa_evidence/2026-09-25-dvv2-scratch-ledger/, docs/qa_evidence/2026-09-25-tryit-DVV2/], github: null, plans: [], related: [SZHQ]}
---
# Relay owns agent scratch: a ledger for every temp dir, a lifecycle with cleanup, no important work in /tmp and no clutter in $HOME

## Issue
relay should impose discipline on tmp dirs. and when agents use tmp dirs, they should have to log it in a ledger, with an organized cleanup process. write a card for organizing that correctly. because two things i dont like that agents often do are (1) put important stuff that we will need in /tmp, where is deleted; and (2) putting stuff in the home directory and cluttering it up.

## Planning notes
**What goes wrong today (this machine, 2026-09-24).**
- *Unbounded /tmp.* `/tmp/claude-1000` was 380 GB: 153 GB of per-session verify builds (fixed in #SZHQ, `8b9410ad`), 112 GB of Claude Code per-session tool output, and ~100 GB of ad-hoc exports named after cards (`pf4k` 21 GB, `r-batchgui`, `v-panefix`, `merge`, `base2` …). `/tmp/relay-*` and `tmp*` held another 23 GB (1,179 entries). Nobody recorded who made any of it or why.
- *Important work in /tmp.* The same trees hold things that were needed later: the land.py snapshots, Try-it staging, QA evidence drafts, message files (`/tmp/claude-1000/frvm-msg.txt`, `2czp-card.txt`). /tmp is wiped at reboot and by tmpfiles, and a GC cannot tell a draft that matters from a build.
- *Clutter in $HOME.* Agent-made top-level folders sit next to the user's own: `~/tmp` (1.1 GB: `atlas-build`, `godot-install`, `mutagen-install`, `prism-validation`), `~/relay-phone` (1.0 GB), `~/projects-reorg` (905 MB), `~/relay-qa`, `~/relay-stash-archive-20260920`, `~/logs`. The user's home is theirs; an agent choosing a path there is guessing.

**Principle.** An agent never picks a scratch path. It asks Relay for one, says what it is for, and Relay decides where it lives, records it, and ends it. Anything that must outlive the task is not scratch: it goes where the project keeps it (the repo, the card's evidence folder), and Relay moves it there, not the agent's memory.

**Design (proposal).**
1. **Three classes, three homes, nothing else.**
   - `scratch`: disposable (exported trees, build dirs, test workspaces, downloads to inspect). Under `$XDG_CACHE_HOME/relay/scratch/<session>/<slug>/`. The cache dir, not /tmp: it survives a reboot mid-task, it is per-user, and cleaners and backup tools already know to skip it.
   - `keep`: output the work needs later (evidence, reports, generated data, message drafts). It is created in the project, `<project>/.relay/work/<card-or-session>/<slug>/` (git-ignored), and promoted on request into `docs/qa_evidence/…` or wherever the card says.
   - `install`: a tool the agent installs for the user (`godot-install`, `mutagen-install`). It goes under `$XDG_DATA_HOME/relay/tools/<name>/` with a ledger row, never a new top-level folder in `$HOME`.
2. **The ledger.** One append-only JSONL per user, `$XDG_STATE_HOME/relay/scratch-ledger.jsonl`. Each row holds: id, path, class, purpose (one line), created_by (session, pane, model, card), created_at, expected lifetime (`task` / `session` / `days:N` / `until-promoted`), state (`live` / `released` / `promoted` / `reclaimed` / `orphaned`), size at last scan. The ledger is what `relay-scratch report` reads first; the directory walk only finds what is *not* in it.
3. **The API.** A `scratch_dir` tool (and `relay-scratch new --class scratch --purpose "…" [--card X]` for shells and guests) returns the path and writes the row. `scratch_release` ends it, or ends it by itself when the task, session or card closes: `scratch` is deleted, `keep` must be promoted or explicitly dropped (the agent is asked, never silently), and `install` stays until the user removes it. The deliver skill's close-out step includes "release your scratch".
4. **Enforcement, in escalating order.** (a) Relay sets `TMPDIR` for agent shells to the session's scratch root, so `mktemp`, Python's `tempfile` and most tools land in a ledgered place without the agent doing anything. (b) The tool layer inspects `write_file`/`run_command` targets: a write under `/tmp` or a new top-level entry in `$HOME` is refused, or rewritten, with a message naming `scratch_dir`. It is refused outright when the file looks like a deliverable (`*.md` report, evidence, a message file). (c) A post-turn sweep lists new top-level `$HOME` entries and new `/tmp` entries owned by the user that were created during the turn and are not in the ledger, and puts them to the agent ("ledger it, move it, or delete it") before the turn can close.
5. **Cleanup process.** Deleting is a ledger transition, never a guess. `task` scratch goes when its task closes, `session` scratch when the session ends, `days:N` at expiry. Before anything is deleted: no live process uses it (the #SZHQ checks), it is not `keep`, and the row is marked `reclaimed` with the size freed. Orphans (on disk, not in the ledger) are listed for the user with their creator guessed from the path and mtime, never deleted automatically. The #SZHQ monitor reports ledger totals by class and by session, and flags `keep` items still unpromoted after their card closed.
6. **Migration.** A one-time `relay-scratch adopt` pass writes `orphaned` rows for what is already there: `/tmp/claude-<uid>`, `/tmp/relay-*`, and the `$HOME` folders above. The user can then keep, promote or reclaim each one from a single list.

**Decisions for the owner.** (i) Refuse, or silently redirect, a write to `/tmp`/`$HOME` (4b)? I recommend refusing with the exact `scratch_dir` call in the message: redirecting hides the lesson and surprises tools. (ii) Default lifetime for plain `scratch`: `session`, or 24 h after last write? (iii) Is `.relay/work/` in the project the right home for `keep`, or should it go straight into the card's evidence folder? (iv) Guests (Claude Code, Codex): set `TMPDIR` in their environment too, and give them `relay-scratch new` through the bridge?

**Done means.** An agent session that builds, tests and writes a report leaves no unledgered entry in `/tmp` or `$HOME` (checked by the post-turn sweep in a test). Its scratch is gone when the session ends. Its report is in the project, not in /tmp. `relay-scratch report` shows the ledger by class and session.

## Done means
An agent that needs scratch asks Relay for it (`scratch_dir` tool or `relay-scratch new`), and every dir it creates is a ledger row in `$XDG_STATE_HOME/relay/scratch-ledger.jsonl`. A session that builds, tests and writes a report leaves no unledgered entry in `/tmp` or new top-level `$HOME` folder (the post-turn sweep test proves it), its `scratch` is reclaimed when the session ends, and its report and evidence live in the project (`.relay/work/…`, promoted to the card's evidence folder). `relay-scratch report` shows the ledger by class and session, `adopt` lists what already exists as orphans, and the #SZHQ monitor reports ledger totals and flags unpromoted `keep` items whose card closed. Scratch is deleted only by Relay, as a `reclaimed` ledger transition — never by another cleaner. Failure looks like: a turn ends with an unledgered `/tmp`/`$HOME` entry nobody was asked about, scratch still on disk after its session ended, or a row saying `live` for a dir that is already gone.

## Plan
**Goal.** Relay, not the agent, owns scratch: three classes with fixed homes — `scratch` under `$XDG_CACHE_HOME/relay/scratch/` (decision (v) below), `keep` under `<project>/.relay/work/`, `install` under `$XDG_DATA_HOME/relay/tools/` — an append-only ledger (`$XDG_STATE_HOME/relay/scratch-ledger.jsonl`), an API agents must use, escalating enforcement, and cleanup where deleting is a ledger transition. Scratch may be disposable, but it is always tracked and cleaned — the owner's requirement verbatim.

**Findings.**
*Already landed in the working tree, uncommitted (verify before landing):* `relay::scratchpaths` in `src/AppPaths.h:87–129` (`RELAY_SCRATCH_HOME`/`RELAY_TOOLS_HOME`/`RELAY_STATE_HOME` over XDG defaults) and `backend/relay_core/scratch.py` — `scratch_root()`/`tools_root()`/`ledger_path()`/`keep_root()`, `ScratchLedger` (:530, append-only JSONL), `end_session()` (:687), `unledgered_created_since()` (:703, the sweep's query), `new`/`release`/`adopt`/`ledger` CLI verbs, ledger-aware `report`/`gc`, and the `MIN_IDLE_HOURS = 6.0` floor with `--force-idle` (:324–343) added after the 2026-09-25 gc incident. `tests/test_scratch.py` 16/16 green.
*Still to build, where it goes:* `scratch_dir`/`scratch_release` tools in `backend/relay_core/tools.py` (spec list ~331–373, `prepare()` :630); the write refusal extends the guard at `backend/relay_core/agent.py:4395`; `TMPDIR` goes into `src/GuestBridge.h:284`, `src/BoardWorker.cpp:115`, `src/RemotePane.cpp:2030` and the pty spawn; the sweep hooks `turnFinished` (`src/AgentContext.h:306`); the monitor is `src/ScratchMonitor.{h,cpp}`; `backend/relay_core/skills_bundled/disk-hygiene/SKILL.md` still teaches `$TMPDIR/claude-<uid>`; and `backend/relay_core/tryit_protocol.py:792` hardcodes `/tmp/claude-{uid}/tryit` — Relay's own Try-it uses the bad pattern.

**Steps.**
1. ✅ **Paths** — landed (above). Remaining: `.relay/work/` into the repo `.gitignore`, one line in `docs/`.
2. ✅ **Ledger** — landed (above), including the idle floor.
3. **CLI check**: confirm `relay-scratch new --class … --purpose … [--card X]`, `release <id|path>`, `adopt` flags match what landed; align names where they drifted.
4. **Tools** (`tools.py`): declare `scratch_dir` and `scratch_release` like the other host tools; they allocate the path and write the row, `created_by` from the session identity the tool layer already holds.
5. **TMPDIR** (four spawn sites + pty): point `TMPDIR` at the session's scratch root under the cache dir, created on demand and ledgered as a `session` row, so `mktemp`/`tempfile` land somewhere owned with zero agent effort.
6. **Write guard** (`agent.py:4395`): a `write_file`/`edit_file` resolving under `/tmp` or creating a new top-level `$HOME` entry is **refused** with the exact `scratch_dir` call in the message; a deliverable-looking path (`.md` report, evidence, message file) outside the workspace is refused outright. `run_command` writes are the sweep's job (step 7).
7. **Post-turn sweep**: record the turn-start timestamp; on `turnFinished` run the landed `unledgered_created_since()` and put each hit to the agent — ledger it, move it, or delete it — before the turn closes. Top-level `$HOME` only, no recursion.
8. **Lifecycle**: `scratch` released at session/pane close and task end (`end_session()`), `days:N` at expiry via `gc`; `keep` never auto-deleted — release asks promote-or-drop; `install` stays until the user removes it. Every deletion writes `reclaimed` with the size freed.
9. **Monitor** (`ScratchMonitor.{h,cpp}`): ledger totals by class and session, unpromoted `keep` whose card closed, orphans listed for the user. No auto-delete; **Clean up** keeps running `gc --apply` (now behind the 6 h floor).
10. **Migration run**: run `relay-scratch adopt` for `/tmp/claude-<uid>`, `/tmp/relay-*`, `/tmp/tmp*`, and the known `$HOME` folders; walk the owner down the resulting list (keep / promote / reclaim each).
11. **Skills, guests, Try-it**: rewrite `disk-hygiene` around `scratch_dir`/the ledger; add "release your scratch" to `deliver`'s close-out; guests get `TMPDIR` via `GuestBridge` and `relay-scratch new` through the bridge; retarget `tryit_protocol.py:792` to a ledgered scratch dir.
12. **Tests and docs**: everything under Verify, plus a short `docs/SCRATCH.md` (three classes, the ledger, the refusal, the override env vars).

**Orchestration.** Steps 1–2 done. Then two subagents in parallel on disjoint files: **A** — Python: steps 3, 4, 6 and their tests (`scratch.py`, `tools.py`, `agent.py`, `scripts/relay-scratch`, `tests/`); **B** — C++: steps 5, 7, 9 (`src/AppPaths.h` read-only for them, `GuestBridge.h`, `BoardWorker.cpp`, `RemotePane.cpp`, `AgentContext.*`, `ScratchMonitor.*`; gate on `scripts/relay-build`). Neither commits. The main agent reviews both, does steps 8, 10, 11, 12, and lands everything through `scripts/land.py`.

**Risks / owner decisions.**
- (i) Refuse vs redirect the guarded write — **refuse**, naming the `scratch_dir` call (Codex's model, per the research note).
- (ii) Default `scratch` lifetime — **`session`**, `gc` as the backstop.
- (iii) `keep` home — **`<project>/.relay/work/`**, promoted on request.
- (iv) Guests — **yes to both** (`TMPDIR`, `relay-scratch new` via the bridge).
- (v) **"Is `.relay/tmp` better?" — no, for `scratch`; `.relay/` stays the home of `keep`.** In-project scratch is deleted behind the ledger's back by `git clean -xdf` (the `clean-commit` skill does exactly that to ignored paths), cannot exist in read-only or shared checkouts or in panes with no project, and puts build dirs inside the source tree every recursive tool then trips over. `/tmp` proper fails differently: `systemd-tmpfiles` and reboots delete without telling the ledger, so a row says `live` for a dir that is gone — tracking cannot fix a second deleter — and on tmpfs machines 20 GB of scratch competes with RAM. The cache dir gives what you asked for — tracked and cleaned — with Relay as the only deleter, per-user, skipped by backups, surviving a reboot mid-task (macOS per-user `$TMPDIR` and Gemini CLI's `~/.gemini/tmp` are the precedents). If you ever want a different root, `RELAY_SCRATCH_HOME` (landed in step 1) moves it per machine — including to `/tmp` — without touching the ledger.
- `TMPDIR` redirection surprises tools that exchange absolute `/tmp/...` paths between processes; land.py's `/tmp/claude-1000/land/` root must stay pinned where `land.py gc` expects it.
- The turn-end `$HOME` scan must stay top-level-only and cheap.
- Cross-platform: all roots go through the `AppPaths.h` `RELAY_*`/XDG pattern, never raw `os.environ`.

**Verify.**
- `tests/test_scratch.py` (landed, green) plus: guard tests (`write_file` to `/tmp/x` refused naming `scratch_dir`; `<project>/.relay/work/…` allowed; `.md` outside workspace refused); sweep test (a scripted turn leaves an unledgered `/tmp` file → the agent is asked, nothing deleted silently); **idle-floor regression** (`gc --apply` under 6 h idle on default roots refuses without `--force-idle` — the 2026-09-25 incident, as a test); reconcile case (row `live`, dir missing → reported, never silent); `report` grouping by class and session; Try-it staging lands under the scratch root, not `/tmp/claude-*`.
- C++ gate: `scripts/relay-build` for the `src/` changes; targeted tests only (`ctest -R scratch`, the pytest files) — suites stay with the owner.
- By hand: a session in the app calls `scratch_dir`, the row appears in `relay-scratch report`, session end leaves `reclaimed` and no dir; `relay-scratch adopt` shows the 380 GB-era leftovers as orphans for the owner to resolve.

## Tests
- `python3 tests/test_scratch_ledger.py` — 29 cases (1 environment skip): row schema; `new` creates dir + row; `release` scratch → `reclaimed` with freed size; keep without flags refused (promote/drop explicit); promote moves the tree and marks `promoted`; `end_session` reclaims session scratch, leaves `keep`; gc writes `reclaimed` transitions and never keeps `keep` removable; **idle-floor regression** (`gc --apply` under 6 h idle on default roots refuses without `force_idle` — the 2026-09-25 incident, as a test); reconcile (row `live`, dir gone → reported "gone from disk", never a deletion candidate); `report`/`ledger_summary` grouping by class and session; `adopt` dry-run lists orphans, deletes nothing, dedupes; `unledgered_created_since` sweep scan; write-guard refusals (`/tmp` write refused naming the exact `scratch_dir` call; hardcoded real-`/tmp` path still refused once TMPDIR is repointed; new top-level `$HOME` entry refused; `.md` deliverable outside the workspace refused; workspace and `<project>/.relay/work/` allowed); TMPDIR hook (one row, reused on restart; keyed on `RELAY_SESSION_TOKEN` when the worker knows it; a conversation reset reclaims conversation scratch, never the pane's tmpdir row).
- `python3 tests/test_scratch.py` — 16/16, unchanged (legacy walk mode still intact).
- `ctest --test-dir build -R scratchmonitor` — 12/12, including two new `parseLedger` cases (totals by class/session, unpromoted keeps, orphans; invalid or empty ledger stands quiet, and an older CLI's output without `live` too).
- Build: full `scripts/relay-build` (130 s, includes the `src/` changes) and `scripts/land.py`'s own verify-slot build of the exact tree that landed (`0da66145`, `acdbb2d0`).
- By hand, sandboxed (transcript in evidence): `relay-scratch new/release/ledger/report`, promote-into-project works, keep refuses silent deletion; try-it root now `<scratch root>/tryit`.
- Not run here (verifier's pass): the in-app hand walk — a pane session calls `scratch_dir`, the row shows in `relay-scratch ledger`, pane close leaves the dir gone and the row reclaimed; `relay-scratch adopt --apply` then the owner resolving the orphan list. Full suites stay with the owner.

## Execution Summary
Landed as `0da66145` (main implementation), `35fa497b` (evidence), `acdbb2d0` (follow-up: try-it root under the scratch home; idle-floor and reconcile tests; ownership-check reorder).

- **Paths** — `relay::scratchpaths` in `src/AppPaths.h`, mirrored in `scratch.py` (`RELAY_SCRATCH_HOME`/`RELAY_TOOLS_HOME`/`RELAY_STATE_HOME` over XDG; `docs/SCRATCH.md` is the contract). `.relay/work/` was already git-ignored by `/.relay/`.
- **Ledger** — `ScratchLedger` (append-only JSONL, last record per id), `new`/`release`/`adopt`/`ledger` CLI verbs, `end_session`, ledger-aware `report`/`gc` (the walk only finds what is not in the ledger; every removal is a `reclaimed` transition; `keep` never auto-deleted, `install` stays until the user removes it).
- **API** — `scratch_dir`/`scratch_release` host tools (declared in `tools.py`, dispatched in `agent.py`, labelled in `tool_labels.py`), plus the CLI for shells and guests.
- **TMPDIR** — the pane shell (`PaneRuntime.cpp`), guest CLIs (`GuestBridge.h`, main + sidecar) and the worker's `run_command` children (backend `os.environ` + `command_env`) all point at `<scratch root>/<pane token>/tmp`, one ledger row per pane (`days:7` — the pane outlives conversations and must survive a reset); `startWorker` now exports `RELAY_SESSION_TOKEN` per process so all three share that one root and row.
- **Guard** — `write_file`/`edit_file` under the system temp dir (either the repointed TMPDIR-relative or the hardcoded `/tmp`, via `system_tmp()` captured at import) or a new top-level `$HOME` entry is refused naming the exact `scratch_dir` call; `.md`/evidence/message deliverables are refused outright outside the workspace.
- **Sweep** — on the model's completion of a turn, `unledgered_created_since(turn_start)` names new unledgered entries to the agent once (ledger it, move it, or delete it) and the turn continues once; bounded, asks only, never deletes.
- **Monitor** — `ScratchMonitor` parses `relay-scratch ledger --json`: totals by class and session, unpromoted `keep` with the `relay-scratch release` suggestion, orphans; Clean up still runs `gc --apply` (behind the floor). Try-it staging root moved off `/tmp/claude-<uid>/tryit`.
- **Skills** — `disk-hygiene` rewritten around the ledger; `deliver` close-out now says "release your scratch".
- **Incident (owned)** — my sandboxed smoke test of `gc` lost its `RELAY_SCRATCH_ROOTS` narrowing and `--idle-hours 0 --apply` ran against the real roots for ~80 s: the old backlog (≥24 h idle) plus ~3.3 GB of 3–7 h scratch was deleted (no live process lost its tree; `land/` was protected as managed). The 6 h `MIN_IDLE_HOURS` floor with `--force-idle` now refuses exactly that call, and it is regression-tested.
- **Deferred, with reason** — a first-class `relay-scratch new` verb in the guest bridge's JSON-RPC tool list (decision (iv), half 2): the twelve published tools are a versioned surface shared with the installed guest extensions, so a thirteenth verb is its own protocol change. Guests already get the ledgered placement through TMPDIR and can call the CLI from their own shell. Belongs on its own small card if still wanted.

## Try it
**Open:** `docs/qa_evidence/2026-09-25-tryit-DVV2/stage.sh` — one agent's scratch life in a sandbox (2 s, rerunnable, no network), against the landed `0da66145`+`acdbb2d0`.

**Task:** Run it and read the nine steps: a pane's TMPDIR lands in its ledgered root without the agent doing anything; `scratch_dir`-style rows appear with class, purpose, card and session; releasing scratch reclaims it (gone from disk); the `keep` dir refuses to be deleted silently and then promotes into the project; `end_session` reclaims what is left; the sweep names the one rogue `/tmp` dir the script planted; and `gc --apply` below the 6 h idle floor is refused with the `--force-idle` message. The full path, with the sealed expectation and staging notes, is in `docs/qa_evidence/2026-09-25-tryit-DVV2/` (`path.md`, `expected.md`, `staging-notes.md`).

**Expected:** see `expected.md` (sealed).

**Question for you:** the script left `/tmp/dvv2-tryit-rogue` (holding `only-copy.txt`) on the real `/tmp` on purpose — that is this card's whole subject in one directory. As the owner, which ending do you want that to have in real use: the agent's turn simply cannot close until it has ledgered, moved or deleted such a thing (strict), or the sweep names it and the turn closes anyway, with the monitor and your next `relay-scratch` run catching it (lenient)? The implementation is strict-but-bounded (one ask per turn, then it closes); say if you want the hard block.
