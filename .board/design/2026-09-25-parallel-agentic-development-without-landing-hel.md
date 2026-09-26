---
id: 3MH4
type: work
status: executing
labels: [feature, workflow, land, git, switchboard, design]
assignee: agent
implemented_by: openai/gpt-6-astra via codex
session: 2e8d13e7-b862-4dff-b518-36ccf1658178
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
verify: {artifact: system, primary: script, also: [probe], human: required, criteria: Two native/guest development panes land independently through the queue; restart preserves work and Live accurately shows queued and recoverable work., sign_off: none, effort: high}
source: Claude pane in relay-terminal, 2026-09-26, after reviewing session da8c2a806ad7450da6674120a858f773
links: {plans: [], commits: [08e1de6e7ad7, 1d468dc76f20, 07edfab39c79, 41c2d09324f3, 4a66674f0728, a18f4dda86ed, bdb8c79683df, fa45e40f438f, 76e1974f0ac4, df24acbc9eaa, 423d4833ca27, 91e5d7d103ea, 373e6c959d35, 4347f4883dd6, f5606d58d763, 792cf7b18fcb, 5d40b83c2239, b2d455429dea, 9c97f2676ffd, 2da4097651d2, 7f566b388764, 26456fb63182, c459f5d603d2, 21d78d12d171, 0eebe8f437ba, 0073503a4d90, ca3c7e3187a4, c692bf221292, d95a11446dcf, 585291ffd8ed, 5d9029b33d2e, a0141e41f493, 65ca8c46204b, e7f541a44fa9, c8dce2511c2f, 9873de343f1a, f304e689cd90], evidence: [], related: [C52H, WNKN, NPCD, CBE6, FYEY, SZHQ, 76QW, AQ6X, V52P, ZPWT, HRF6, BHJZ], github: null}
---
# Parallel agentic development without landing hell: isolate by construction (a workspace per session), integrate by machine (a merge queue), for any project

## Issue
The owner wants a system for robust, highly parallelized agentic development that works for other projects, not only this checkout. This card holds the analysis of the last week's landing trouble (session da8c2a80's stuck #C52H landing, the one-slot verify pool, the hunk-attribution machinery in land.py) and a proposal: give every agent session its own git worktree so authorship is by construction, and land through a merge queue that serializes verification on the host, so land.py's snapshot/marker/FOREIGN/digest machinery becomes unnecessary.

> put all these notes on a card. 
>
> the maing thing is, i want to build a solution that will allow robust highly parallelized agentic development.
>
> analyze the landing hell we have been having and see, what system can we develop here that is gonna work for other projects
> — elliott · [session:dbc9af56def4427c91f4fecd2d277da4](relay://session/dbc9af56def4427c91f4fecd2d277da4) · 2026-09-25

## Discussion points
### 1. What happened in session da8c2a80 ("Landing live tab board change", #C52H, 2026-09-25)

- The agent (glm-5.3-flash) repaired `tests/boardpane_test.cpp`, re-based its land session with `begin --base main`, and got through the hunk review: 21 hunks in `src/BoardPane.cpp`, 7 FOREIGN (pane f9635e9a, #YJ4A) and 14 CONTESTED; it excluded two contested hunks that were #YJ4A's `RowList` additions the journal had missed.
- Its first confirmed `commit` failed the verify build in `tests/xcxd_ui_cases.h` / `h2kq_cases.h` — another session's `consolemode` work, which the shared `build/` masked with stale objects. It switched to a narrower `--verify-cmd` (board targets only).
- Three `commit --confirm` runs followed. None landed: each was still waiting or building when the owner stopped the turn ("Not completed: the turn stopped before this tool call finished"). `main` moved twice meanwhile (#YJ4A landed), so each retry recomputed the review and the digest. The agent piped every run through `| tail -8`, which hid the one line that explained the wait: `verify: all 1 build slot(s) busy; waiting for relay-terminal-1006c7a3-0`.
- Session cost: 134 requests, 10.1M prompt tokens (9.3M cached), $0.65 — for a change that never landed. #C52H is still executing.

### 2. Why there was one verify slot

- `verify_slots()` = `min(4, disk/3 GiB, memory/8 GiB)`, clamped to ≥1. `memory_for_slots()` prefers the calling process's cgroup cap (`scripts/relay-build` `memory_limit_bytes()`), and every agent pane runs in a systemd scope with `MemoryMax`.
- `~/.config/RelayTerminal/relay.conf` still says `agent_memory_max=8G` (the owner raised the *total* `app-relay.slice` ceiling to ~110 GiB, not the per-agent cap). 8 GiB ÷ 8 GiB = 1 slot, on a 122 GB / 2.8 TB machine.
- The pool is machine-wide but sized by whichever pane asks: a pane with a small cap shrinks it for everyone. And the slot count is per repository (`slot_prefix(repo)`), so N projects landing at once would each get their own pool — four busy projects could exceed the host budget.
- At the time of inspection: `wbfm-b try` held the slot (14+ min, `cmake --parallel 4`); `83yv-codex commit`, `ssrq commit`, `8531c4b7 try` and `board-live-tab commit` were queued behind it. A dead pid still owned slot 1's lock record.
- Short-term fixes, in order: set **Agent memory limit** to Auto (≈61 GiB here) in Relay's settings (applies to new agents); size the pool from host memory (`MemTotal`) and keep the cgroup cap only for `--parallel`; print the busy line at once naming holder, pid, age and queue position; add `--wait-seconds N` with its own exit code; poll all slots instead of blocking on the most-recently-used one; and tell agents not to pipe `land.py commit`/`try` through `tail`.

### 3. The landing week in numbers (2026-09-18 → 09-25, from the 1,400 saved sessions and git)

- 2,598 commits on `main` in 8 days (219–568/day). 290 sessions ran `land.py commit`/`try`; 2,327 calls in all; the median such session ran it 6 times, 8 sessions ran it 20+ times.
- Outcomes of the 2,327 calls (classified from tool output): ~527 clearly landed; 343 stopped at the review hold (exit 4, digest, confirm); 57 failed the build/test gate; 13 aborted on a merge conflict; 5 sat visibly in a slot wait; 4 interrupted by the owner. Text mentions across outputs: `contested` 484, `moved` (main moved under the digest) 126, `stale` 89, `conflict` 79, `refused` 63, `FOREIGN` 22.
- Wall time: `commit` median 0.24 s (Python-only, no gate), p90 23 s, max 880 s; 127 minutes of recorded commit time; `try` median 39 s, p90 517 s, max 988 s.
- Even with land.py, **8 `repair: take back …` commits** in the week: landings that put wrong content on `main` and had to be undone (#9FX8, #NXN0 twice, agent.py, FilePanes, a 15-file plugin landing, the protocol doc).
- `land.py` is 3,835 lines plus 1,924 test lines, changed by 19 commits in 7 days, with 12+ cards about it (#SZHQ, #WNKN, #76QW, #FYEY, #HRF6, #BHJZ, #MJ76, #NPCD, #CBE6, …). `CLAUDE.md` spends ~200 lines teaching it. The working tree holds 592 dirty paths right now (142 outside `.board/`).
- Main is not green: 10+ open cards are "X fails at HEAD" (#SYTR, #JK3T, #QAJQ, #T9K3, #15ZE, #D9AQ, #5P0Q, #FV0P, #WMX7, #PWY9). So the gate cannot run the suite, so agents narrow it (`--verify-tests`, `--verify-cmd`), so more breakage lands — a loop.

### 4. Diagnosis: every land.py mechanism is compensating for one decision

The 09-18 rule — one shared checkout, one shared index, one shared `build/`, no branches, no worktrees — removed git's isolation to solve a *coordination* problem (invisible side-branch work, the same bug fixed twice, stale worktrees). Everything since re-creates isolation by hand, one level down, where it is much harder:

| Lost by sharing | Rebuilt by land.py | Cost |
|---|---|---|
| Whose bytes are these? | snapshots, markers, CONTESTED, FOREIGN, per-token authorship journals | attribution by *inference*; 484 contested reviews; still 8 repairs |
| A private index | private index + commit-tree + compare-and-swap + pre-commit hook | 10 retries when main moves; digests invalidated 126× |
| A tree that is only my change | materialise the exact tree in a verify slot | serialized builds; a pool sized by one pane's cgroup |
| A build that reflects the source | `relay-build` lock + timestamp stamping | still masked #C52H's real break |
| Pick what to land | positional `--only-hunk`/`--exclude-hunk` | numbers shift (#NPCD); agents mis-select |

Second-order effects: agents spend turns operating the tool instead of the code (this session: 3 confirms, 0 landings); the protocol is bespoke, so it cannot move to another project without moving 3.8k lines and 200 lines of rules; and the verify bottleneck is serialized on one host while development is parallel.

### 5. Proposal: isolate by construction, integrate by machine

A system that generalizes has to be project-agnostic on three points: it must not need a custom commit tool, it must not need the agents to learn a review vocabulary, and it must let a project say only "how to build and test me".

**A. A workspace per session (attribution by construction).** When a pane claims a card, Relay creates a git worktree for it under Relay-owned state (`~/.local/state/relay/worktrees/<repo>/<session>`, branch `relay/<card>-<session>`) and makes it the pane's cwd. Every byte in that tree is that session's; `begin`, snapshots, markers, FOREIGN and the authorship journal have nothing left to decide. The three objections that motivated the ban are answered by the Board, not by sharing a tree:
- *Invisible work* → the Board's Live tab lists every live worktree with its card, branch, diff stat vs main and age; `git show <branch>:<path>` reads anyone's version; the reap/orphans machinery (#FYEY) already knows how to keep a dead pane's work.
- *Same bug fixed twice* → the card claim is the lock (it already is); a pre-flight `board_list` + "these paths are being changed on branches X, Y" warning at claim time.
- *Stale branches and build dirs* → Relay owns the lifecycle: the worktree is removed when the card lands or the pane is reaped; build dirs share `ccache` (#V52P) so a fresh worktree is mostly cache hits.

**B. A merge queue (integration by machine).** `main` gets exactly one writer: a queue daemon in the Relay backend. An agent's `land` is a *submission*: branch + card + the tests it names. The daemon rebases onto tip, builds the exact tree in a host-owned slot pool (sized from `MemTotal`, shared across every project on the host), runs the project's verify command and the selected tests, and fast-forwards `main` — or hands the failure back to the agent as a card comment and a rebase task in its own worktree. A conflict is a normal `git rebase` in a private tree, not a hunk puzzle. Batching (bors-style) amortizes builds: verify N queued branches together, bisect on failure. Python-only or docs-only submissions take a fast lane with no build.

**C. Keep main green as a queue property.** Nothing lands that failed its verify; the queue keeps a warm build of tip so every verify is incremental; a nightly full run files signal cards (#AQ6X) for anything red at HEAD; test selection from the changed paths (CMake target graph, Python import graph) keeps per-landing cost near the change's size.

**D. Board writes are not code.** `.board/` writes land by the backend on their own (#CBE6), not through the queue, so the 592-dirty-path checkout stops being everyone's problem.

**E. Per-project configuration is one file.** `.relay/land.toml` (or a `relay:` key in an existing config): `verify = "scripts/relay-build"`, `tests = "ctest --test-dir build -R {selected}"`, `fast_lane = ["*.md", "backend/**/*.py"]`, `select_tests = "scripts/select-tests.py"`, `max_branch_age = "6h"`. A project with nothing configured gets: worktree per session, queue with `git merge --ff-only`, no gate. That is what makes it work for other projects: the mechanism is git + a daemon, the project supplies two commands.

**F. Migration, incremental.** (1) A project setting turns on worktree-per-claim; the shared checkout stays for humans and for reads. (2) `land.py submit` pushes the branch to the queue; `commit` keeps working for the shared checkout meanwhile. (3) The queue daemon lands; `land.py`'s hunk machinery is retired for worktree panes. (4) The `CLAUDE.md` landing chapter shrinks to "work in your worktree; `land submit` when green".

### 6. Trade-offs the owner should weigh

- **Disk and warm builds.** A worktree is source only (~200 MB); a build dir per worktree is ~3 GB. With `ccache` and the queue doing the authoritative builds, agents can `try` in their worktree at cache-hit cost; the queue's slots are the only full builds. Bound: N worktrees × source + K slots × build, with K a host property.
- **Branch drift.** Long branches are the classic worktree failure. Mitigation: auto-rebase on every submit, a `max_branch_age` warning on the Live tab, and small cards.
- **Throughput.** At 250–550 landings/day and a 5–15 min C++ verify, a single serialized queue needs batching or test selection to keep up; today the same load is met by *skipping* the gate for most commits, which is why main is red.
- **Who lands `.board/`.** Decide whether board writes auto-commit per write or on a timer (#CBE6).
- **What to keep from land.py.** `repair`, `doctor`, `who`/`status`/`orphans`, `board-sync` and the pre-commit hook stay useful; the snapshot/marker/FOREIGN/digest layer is what the worktree makes redundant.
### 7. Worktree cost, measured here (2026-09-26) and projected for large repos

Owner asked: "how much disk / overhead / complexity will the worktrees bring in large repos".

**This repo.** `git worktree add --detach main`: **1.7 s**, 420 MB RSS, **502 MB** on disk — of which 461 MB is `docs/qa_evidence/` screenshots (451 MB of tracked images); the code itself is ~40 MB. The 695 MB `.git` (612 MB pack) is shared by every worktree, not copied. A full C++ build dir is **3.4–3.7 GB** (509 objects); `build-fast` is 534 MB. So the source checkout is not the cost; the build dir is, by 7×.

**The build cache is thrashing.** `~/.cache/relay/ccache` is at its 10 GB `max_size` (100% full) with a **39.8% hit rate** (6,952 / 17,457). With two verify slots plus `build/` plus `build-fast/` plus `build1/`, `build-engine/`, `build-clean/` already on disk (a further 1.4 GB of stray build dirs in the checkout), the cache cannot hold one full configuration. On a 2.8 TB-free ext4 disk the limit should be 100 GB+; at that size a fresh worktree's build is mostly hits. ext4 here has **no reflink**, so a warm build dir cannot be cloned for free (btrfs/XFS could).

**Bounds per worktree, generalized.** disk = checkout + (build dir if built locally) + language-specific deps; time = `worktree add` ≈ checkout time (scales with file count, seconds up to ~100k files; the index is per worktree).
- Small/medium (this repo, most services): 50 MB–1 GB source, 1–2 s. Negligible.
- Large C++ (LLVM/Chromium-class, 100k–300k files): 1–4 GB source, 10–60 s add; a full build 10–50 GB — unaffordable per worktree, so **the queue's K host-sized slots do the full builds**, agents' `try` submits to the same slots, and local builds go through a properly sized ccache/sccache. Per-worktree sparse-checkout (git ≥ 2.25, `--sparse`) cuts source to the subtree a card touches.
- Monorepos (10s of GB): partial clone + sparse worktrees are the only workable shape; the same rule — build in slots, not in trees.
- JS/Python repos: the cost moves to `node_modules`/`.venv` per worktree (0.5–2 GB, minutes to install). Mitigate with a content-addressed store (pnpm, `uv` cache, a shared venv symlinked in by the project's init hook).

**Overhead that is not disk.** Each worktree has its own index and `git status` scan (fsmonitor fixes the cost at 100k+ files); untracked-but-needed files (`.env`, local config, `build/`) do not follow into a new worktree — a per-project **workspace init hook** ("after creating a worktree, run …") is required and is the one genuinely new piece of configuration; tools that assume the repo root is the one checkout (hard-coded paths, IDE indexes, LSP) see a second root; the pane's cwd is no longer `~/repos/<name>` (either accept `~/.local/state/relay/worktrees/<repo>/<session>` or put worktrees under `<repo>/.worktrees/` gitignored).

**Complexity budget, honestly.** Lifecycle (create on claim, `prune` on reap, delete the branch after landing, `worktree lock` for a pane that is suspended), the init hook, the Live-tab listing, and the queue. Against it: the snapshot/marker/CONTESTED/FOREIGN/journal/digest layer of land.py and the 200 lines of `CLAUDE.md` that teach it. Net: less machinery, and machinery that is plain git.

**Do first, regardless:** raise `ccache max_size` to ≥100 GB; delete `build1/`, `build-engine/`, `build-clean/` from the checkout; size the verify pool from host memory.
### 8. Day by day: it did get worse on 09-24/25, and the data says why

Owner: "it seems like this issue got a lot worse in the last 2-3 days. can you tell how it was working early on". Per-day `land.py commit`/`try` calls from native-pane session checkpoints (guest Claude Code/Codex transcripts live elsewhere and are **not** in these counts; `git log` is complete):

| day | sessions using land.py | calls | review holds | gate failed | conflicts | `moved` | FOREIGN | slot waits | p90 s | max s | wait min/day | commits on main | repairs | land.py changed |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 09-19 | 35 | 281 | 84 | 9 | 0 | 16 | — | 0 | 14 | 49 | 15 | 382 | 1 | 7× |
| 09-20 | 38 | 312 | 86 | 8 | 1 | 16 | — | 0 | 20 | 61 | 18 | 549 | 1 | 1× |
| 09-21 | 5 | 37 | 14 | 0 | 0 | 1 | — | 0 | 1 | 41 | 2 | 568 | 0 | 0 |
| 09-22 | 12 | 95 | 26 | 5 | 1 | 5 | — | 0 | 3 | 47 | 3 | 262 | 0 | 0 |
| 09-23 | 53 | 389 | 106 | 10 | 9 | **62** | — | 0 | 3 | 68 | 3 | 322 | 1 | 1× |
| 09-24 | 61 | 515 | 53 | 14 | 1 | 14 | — | 1 | **43** | 72 | **32** | 219 | **4** | 3× |
| 09-25 | **88** | **739** | 91 | **31** | 2 | 18 | **40** | **15** | 29 | **988** | **83** | 264 | **3** | **7×** |

(`CLAUDE.md`: 272 lines on 09-19 → 315 on 09-24 → 367 on 09-25.)

**How it worked early (09-19–22).** ~300 calls/day from ~35 sessions; p90 under 20 s; no queueing at all, because every session had **its own** verify build directory; 1 repair/day; the review hold fired on ~28% of calls and was mostly answered in one `--confirm`. The costs were hidden: disk (per-session verify builds reached 153 GB by 09-24, card #SZHQ) and silent mis-attribution (which is why CONTESTED was added on 09-19).

**What changed on 09-23.** Concurrency: 53 sessions, and `moved` (main moved under a digest) jumped 5 → 62 in a day. Attempts per landed commit rose from under 1 (09-20: 312 calls for 549 commits, many landed from guests) to 1.2, then 2.4 on 09-24 and **2.8 on 09-25** — each commit now takes ~3 invocations.

**What changed on 09-24.** #SZHQ replaced per-session verify dirs with a shared slot pool, sized by the caller's cgroup — which, with `agent_memory_max=8G`, is **one slot**. That fixed the disk and introduced the queue: p90 14 s → 43 s, total wait 15 → 32 min/day, first slot wait, 4 repairs in a day. ccache (#V52P) arrived the same day with a 10 GB limit that is already full at a 40% hit rate.

**What changed on 09-25.** Load 2.4× 09-19 (88 sessions, 739 calls); FOREIGN/authorship journals/auto-claim adoption (#WNKN) shipped and flagged 40 hunks on day one; `land.py` was changed 7 times in the day and `CLAUDE.md` grew 52 lines, so agents were operating a moving protocol; 15 visible slot waits, one 988 s call, 83 min/day of agents sitting in `land.py`; gate failures 31 (main red: the 10 "fails at HEAD" cards). Commits/day meanwhile fell from 549–568 (09-20/21) to 219–264.

**Reading.** Three curves crossed at once: sessions per day (35 → 88), a verify pool that shrank from N private dirs to 1 shared slot, and an attribution layer that grew a rule per incident. The early period worked because concurrency was low and verification was unbounded; it was never scalable, only under-loaded. Per-worktree isolation plus a host-sized queue is the shape whose cost stays flat as sessions grow.
### 9. "YOLO simultaneous editing + a dynamic reconciler" (owner, 2026-09-26)

Owner: "what if we just allow YOLO simultaneous editing and then a dynamic reconciliation system that fixes conflicts and errors on the fly, sort of like dealing with merge conflicts. AI is really good at that these days."

**Agreed on the premise.** Models resolve three-way conflicts well *when they are given both sides and both intents*; fixing a compile error after a merge, adapting a call site to a rename, re-applying a hunk whose context moved — all of that is cheap now (one model call per conflict; at ~80 contested events/day it is noise next to the 83 min/day agents currently spend inside `land.py`). The reconciler should exist. The question is only what it is given to reconcile.

**Where it fails if the tree is shared.** "YOLO editing" is what we have today: one tree, everyone writing. The trouble was never the editing, it was the commit — which bytes form one coherent, buildable change. A reconciler does not remove that question, it inherits it:
- *A merge needs sides.* A conflict is "A wanted X, B wanted Y". In one shared tree there are no sides, only the current soup; the reconciler would first have to reconstruct A and B from journals and snapshots — which is exactly the inference layer (`CONTESTED`, `FOREIGN`, authorship journals) that has cost 12 cards and still let 8 wrong landings through. Worktrees are what *give* the reconciler sides for free.
- *Errors on the fly are indistinguishable from work in progress.* A file half-edited by a live agent looks like an error. A reconciler that "fixes" it becomes a third writer fighting the author (the 09-19 `Pane.h` incident was two writers; three is worse). It can only safely act on a tree nobody else is typing in.
- *The dangerous conflicts are the silent ones.* Of the 8 `repair:` commits, none was a textual conflict; each merged cleanly and was wrong (half of someone's feature, a stale blob). A reconciler catches those only through build + tests — and main has 10 open "fails at HEAD" cards, so that signal is weak until the queue keeps main green.
- *Accountability.* If a reconciler rewrites what landed, the card record ("session X did Y") blurs and `repair` gets harder. Fine if every reconciliation is a recorded commit with a trailer and a note on both cards; not fine as silent edits.

**The shape that keeps the YOLO experience.** The agent edits freely and never thinks about anyone else — in its own worktree (1.7 s, source-only). Then:
1. **Background rebase, continuously.** The backend rebases each live branch onto tip whenever tip moves (or at least tells the pane "main moved in files you touch: …"). Conflicts arrive small and early, in the agent's own tree, where a wrong resolution breaks only that tree. This *is* "dynamic reconciliation on the fly", placed where it is safe.
2. **`land` is one word.** No hunks, digests, holds, `--confirm`. The queue rebases; clean → build/test → ff.
3. **Reconciler stage in the queue.** Textual conflict → a model gets both diffs, both cards' intents and the conflict; resolves; build/test; green → lands with `Reconciled-From: <A> <B>` and a note on both cards; red → hands the attempt back to the author as a starting point. Semantic break after a clean merge (build or test fails) → the same model gets the error and both diffs, bounded retries (2), else back to the author.
4. **Main stays green** because nothing lands red; the reconciler's fixes are checked by the same gate as everyone else's.

That is the owner's proposal with the reconciler standing between branches instead of inside a shared tree. If the owner would rather keep the single tree, the reconciler degrades to "land.py + a model in the loop for contested hunks" — incremental, but it keeps every mechanism in §4.

**A cheap experiment before deciding.** The land root still holds base/ours/theirs for this week's landings. Replay the 13 conflicts and the 8 repaired landings through a reconciler prompt (both diffs + card intents) and score whether it produces what the authors eventually landed. That measures "AI is really good at that" on our own conflicts in an afternoon, and it does not need the worktree decision first.
### 10. Disk: nobody copies build trees (measured 2026-09-26)

Owner: "is there a way to optimize copying trees that wont use up 100GB". The 100 GB was a ccache sizing suggestion, and it was too generous; the real budget is below.

**What a 3.7 GB build dir is.** Objects 1.47 GB (509 `.o`, `-O2 -g`); **121 executables, 1.37 GB** — every test binary links the whole program with full debug info (`relay` 243 MB, `relay-consolemode-tests` 129 MB, …); archives 0.29 GB; the rest is CMake/autogen. `build-fast` (`-O0 -g1`) is 0.34 GB of objects and 0.12 GB of binaries: **7× smaller**.

**Source trees are not the cost.** A worktree shares `.git`; the checkout is 502 MB only because of 461 MB of `docs/qa_evidence/` images, and a sparse worktree that excludes that path is ~40 MB. Fifty live sessions ≈ 2 GB.

**The separation is the disk answer.** Development trees never hold a build of record; the queue's K slots are the only full builds: K ≤ 4 → ≤ 15 GB, **fixed, whatever the session count**. Slots stay warm (only changed blobs are rewritten, as `land.py` already does), so a landing compiles its own change plus header fan-out, not the world.

**Local builds, when an agent wants one:** the `-O0 -g1` configuration (0.5 GB) through ccache. One full configuration's objects are 1.47 GB raw, ~0.4–0.5 GB compressed in ccache, so **30–40 GB** of ccache holds dozens of header states across both configurations; the current 10 GB is full and thrashing at a 40% hit rate. Not 100 GB.

**Copy-on-write, checked on this host:** ext4 has no reflink (`cp --reflink` fails); unprivileged overlayfs is unavailable (`unshare -Urm` cannot write `uid_map`); hardlink copies (`cp -al`) are unsafe because gcc truncates output files in place, which would corrupt the shared object. So file-level CoW here *is* ccache; reflink-cloning a warm build dir in milliseconds would need a btrfs/XFS volume for the slot pool — optional, not required.

**Project-side cut worth its own card:** the 1.37 GB of test binaries. `-gsplit-dwarf` (or `-g1` for tests), or linking the big code once as a shared library instead of into 121 executables, would shrink every slot and every link. Independent of the worktree decision.

## Plan
**Goal.** Separate development from authoritative verification: each development session owns a workspace; one durable integration service publishes verified changes. Start with an opt-in, single-host implementation that also works in a second Git project. This is a design refinement, not authorization to change this checkout's current no-worktrees rule or machine settings. Earlier Discussion points remain historical analysis; this Plan supersedes their implementation recommendations.

### Findings — current code and corrections

- `scripts/land.py::memory_for_slots` still takes the caller's cgroup cap before host available memory; `verify_slots` caps at four; `slot_prefix` partitions slots by repository. A host scheduler must replace per-project admission, not just substitute MemTotal.
- `scripts/land.py::cmd_board_sync` independently updates `refs/heads/main` and the shared index. `backend/relay_core/board_tools.py` records Board paths for turn-end sync. Board writes must join the publication coordinator if it is to be the sole writer.
- `backend/relay_core/guest_launch.py` passes cwd into guest startup and settings. Moving the GUI's workspace alone cannot move an already-running shell, guest, or tool process.
- `docs/BUILDING.md` describes a 10G compiler-cache default and differing build configurations. Cache size alone does not establish a high hit rate.
- The proposed trees/queue modules are new components. Historical counts in Discussion points are observations from that investigation, not refreshed measurements or guarantees. A passing selected test set does not prove the full suite is green.

### Design contracts

**Workspace ownership and placement.** Keep the proposed location: `$XDG_STATE_HOME/relay/trees/<repo-id>/<workspace-id>/`, outside the source checkout and cache. Identify the repository by its canonical Git common directory and a persistent registration ID, not by the pane cwd; relocation requires an explicit registry repair. Linked worktrees share objects/history and depend on that common directory remaining available. Each workspace has a private branch/index, one writer lease, and session/card metadata; a card ID is not its filesystem identity. New development panes without cards get a workspace too. Read-only planning need not allocate one.

Registry and submissions live in a transactional database under `$XDG_STATE_HOME/relay/integration/`; run logs and recovery refs are durable state. Disposable builds and compiler caches live under `$XDG_CACHE_HOME/relay/` with explicit budgets. Queue records include repository, base SHA, submitted SHA, candidate SHA/tree, verification policy hash, run status, logs and publication receipt.

Sparse checkout is optional and project-configured: do not call an arbitrary exclusion list a cone. Initially support full checkout and tested non-cone exclusion patterns for `.board/` and unrelated QA evidence; preserve instruction/config files and explicitly include the session's evidence paths. Verification uses the full candidate tree unless the project's gate explicitly supports sparsity. Board commands resolve the registered canonical Board root, including CLI fallback; a missing `.board/` in a workspace must never create a second Board.

Init hooks run only for an opted-in project, record failure, and are retryable. No automatic copying of secrets or sharing of mutable virtual environments. Immutable dependency caches may be shared; installed environments must be isolated or keyed to compatible lockfiles/toolchains.

**Submission and queue.** `submit` accepts a committed SHA and stable request ID; it does not silently commit every dirty file. It returns a durable job ID promptly, so agent turns do not wait on builds. Repeated submission of the same request is idempotent. Uncommitted/new work remains in the development tree, and branch movement after submission cannot change that job.

State transitions: `queued → preparing → verifying → ready → publishing → landed`, with explicit `conflict`, `failed`, `cancelled` and recoverable `interrupted` outcomes. A per-repository publisher and a host-wide resource scheduler have separate responsibilities. MVP verifies one candidate at a time per repo, with fair scheduling across repos; no batching yet.

Build a candidate from the recorded submission onto the recorded target tip in a service-owned workspace. Never rebase the author's branch as a queue side effect. Verify the candidate; publish that exact commit using an expected-old-SHA compare-and-swap. If main moved, prepare and verify a new candidate; never attach an old pass to changed bytes. A conflict returns base/submission/target SHAs and diagnostics for the author to resolve.

Persist intent before publication. On restart, inspect the target ref and saved candidate before retrying; recover a landed receipt without duplicating the commit. Notification/card updates use idempotent delivery and retry independently of Git publication. Cancellation before publication prevents landing; cancellation racing a completed publication reports the landed receipt instead of claiming to undo it.

**One publication path, including Board updates.** Board tools still update the canonical Board immediately. Their Git snapshots become metadata jobs handled by the same publisher, coalesced to avoid starving code jobs. They receive a path/schema gate and run between code candidates in MVP; they cannot move main during code verification. Queue notifications may create later metadata jobs without invalidating an already-recorded code result. `board-sync` and legacy code landing must delegate to the coordinator after cutover.

Never update a branch behind a human checkout still attached to it: ref updates alone do not synchronize its files/index. At cutover, after preserving existing changes, move the user's checkout to a human development branch, leaving the integration target service-owned. Refresh that checkout only through an explicit clean-tree sync. Existing uncommitted work must not be reset or silently adopted.

**Verification and resources.** Project config declares the integration target, workspace init/sparse policy, required gate, optional test selector, and resource requests. Agent-selected tests can add coverage, never remove required checks. Python changes run Python checks; docs skip compilation only under an explicit allowlist and still receive applicable checks. Unknown paths and build/config/dependency changes use the conservative gate. Config changes cannot weaken the gate approving themselves: use the previously accepted policy until a separately reviewed policy update is installed.

No-config mode provides workspace isolation and configuration guidance; automatic publishing is disabled until a gate or explicit ungated mode is chosen. Ungated receipts say unverified. Known failures at HEAD need a recorded repair/quarantine policy with owner and expiry before rollout; the queue guarantees its required checks, not absence of all defects.

Scheduler admission uses a configured host budget bounded by the service's effective cgroup/OS limits, CPU, available memory and free disk with headroom. Each admitted job reserves resources; zero capacity means wait with a reason, never clamp to one unsafe build. Development `try` uses the same host admission, at lower priority with starvation protection. UI reports queue age, holder, resource reason and cancellation; service ownership survives pane exit. Support platform locking/resource adapters; unsupported hosts refuse publication clearly.

Warm builds are keyed by repository, toolchain, configuration and dependency inputs. Ensure source changes/deletions invalidate artifacts, and periodically compare a warm result with a clean build. Compiler cache is optional. Budget source, deps, local builds, retained work, slot caches and logs separately. The old 57 GB estimate grows with session count and omits dependencies; retain it only as a scenario estimate, not a bound.

**Retention.** A clean worktree can contain unlanded commits. GC requires a released lease, no dirty/untracked/ignored user data, no unsent or post-submission commits, no pending job, and a recorded retention decision. Rebasing changes SHAs, so use the publication receipt to establish submitted work was integrated. Keep abandoned/crashed work recoverable; show it in Live and enforce quotas through admission rather than deleting work. Branch removal happens only after releasing its worktree and proving no newer work remains.

### Steps — deliverable and exit condition

1. **Stabilize and baseline independently.** Measure queue/slot waits, current cache behavior and existing red checks. Improve legacy wait reporting/timeouts and resource accounting with targeted `tests/test_land.py` coverage. Cache resizing, memory-setting changes and deletion of old builds are separate operational actions, not automatic prerequisites.
2. **Workspace lifecycle library and CLI.** Add proposed `backend/relay_core/trees.py`, registry, `scripts/relay-tree`, init/sparse support and safe retention. Temporary-repo tests demonstrate two writers editing one file independently, dirty and clean-unlanded recovery, repo relocation failure, init failure and quota refusal. No production cutover yet.
3. **Durable queue and publication coordinator.** Add proposed `backend/relay_core/landq.py` and `scripts/relay-land`; immutable submit/status/cancel, scheduler, required gate, receipts and Board metadata jobs. Test crashes at every state boundary, duplicate submit, conflicts, tip movement and two competing service instances. Gate: the published SHA is exactly the verified candidate and restart loses no submission.
4. **Pane/native/guest integration and visibility.** Separate canonical project/Board identity from execution cwd. Allocate before launching development processes; existing panes migrate only at a stopped turn boundary with controlled restart. Update guest context, file tools, shell, scratch and relevant protocol events together. Live shows workspace, submitted job, queue reason and recoverable work. Test native and guest parity plus a no-card pane.
5. **Project configuration and second-project pilot.** Define versioned `.relay/project.toml` from step 2, with opt-in independent of file existence. Pilot Relay's wrapper-based CMake gate and a small Python repo. Add `docs/TREES-AND-LANDING.md` and update `docs/BUILDING.md`; demonstrate that the second repo needs config, not Relay-specific code.
6. **Finish the first milestone: cutover-ready verification.** Obtain fresh independent acceptance at the final B1 SHA, including durable handoff and Board snapshot behavior, plus an isolated migration and rollback rehearsal. Record exact SHAs and limitations. Do not activate this repository or move its checkout branch during this milestone.
7. **Separate production cutover and pilot.** At a later authorized cutover, inventory live legacy claims and dirty work, drain writers, record baseline and accepted policy, and follow `docs/PARALLEL-DEVELOPMENT-MIGRATION.md`. After activation, measure a week of matched submissions, queue wait, gate time, repairs, disk and recovery outcomes before widening rollout. Historical 83 minutes/day is not comparable until collection covers the same guest/native population.

### Risks and later work

Shared-file isolation does not resolve semantic conflicts, duplicate work or missing tests. Keep Board claims, dependency links and overlap notices. Local Git access is not a security boundary: hooks prevent accidental bypass; external writers are detected by expected-tip checks and force re-verification.

Defer background auto-rebase, batching/speculative verification and graph-based test selection. Idle is not consent to rewrite a branch: use main-moved notices and explicit sync under the workspace lease. The owner approved automatic reconciliation: a subscription-weighted High-tier model attempts a bounded repair, the required gate runs again, and unsafe or exhausted cases return to the author agent. No owner acknowledgement or replay experiment gates this flow.

The owner approved the state-owned tree, canonical Board, no-card isolation, weighted High reconciler, runnable `main` release and first implementation milestone. The actual repository remains on its legacy publication path until a separate production cutover; the migration guide governs that transition.

### Verify

Use new focused temporary-repository tests for tree lifecycle and queue state transitions, plus targeted existing land/Board/guest tests for touched adapters. Required scenarios: two same-file authors, dirty and clean-unlanded pane death, restart before/after publication, duplicate submit, competing publishers, target-tip race, Board write during a build, failed/zero-test gate, resource exhaustion across two repos, stale build artifacts, cancellation, newer edits after submit, and rollback with pending work.

The implementation verifier stages native and guest pane behavior in isolated state, captures Live/queue status, and checks independent submissions and recorded verified SHAs. C1 recorded 9 regression passes and 2 LiveGui passes at `e7f541a4`; fresh final acceptance including `c8dce251` and a separate isolated migration/rollback rehearsal remain pending. C2 exercised a second project through configuration and CLI. No production migration has run.

## Tasks

- [x] Stabilize legacy slots and preserve the 40 GB compiler cache; completed in 08e1de6e and 1d468dc7. <!-- t:6w -->
- [x] A1 workspace lifecycle, registry, sparse exclusions, ordered init, sync and safe retention; cd788651/f4545758, 59 tests. <!-- t:kq card=RT3B -->
- [x] A2 immutable queue, exact-candidate publication, receipts and crash recovery; 41 tests. <!-- t:yj card=FW1C blocked_by=kq -->
- [x] B2 native/guest execution and canonical Board; B3 prelaunch allocation, Live status and Relay (main) launcher, with parent fixes `a0141e41` and `e7f541a4`. <!-- t:0w blocked_by=kq,yj -->
- [ ] After MVP: explicit safe sync first; evaluate opt-in background rebase under a workspace lease. <!-- t:zp s=deferred blocked_by=z9 -->
- [x] ~~Replay experiment dropped by owner: "forget the experiment".~~ <!-- t:2f s=dropped blocked_by=yj -->
- [x] A4 weighted High-tier automatic reconciler; persistent attempt/token budgets, test-preservation checks and author handoff; 4347f488, 44 tests. <!-- t:4m card=P9ZA blocked_by=vw -->
- [ ] Fresh final acceptance and isolated cutover/rollback rehearsal for cutover readiness; actual production cutover is separate. <!-- t:c6 s=in-progress blocked_by=6k -->
- [ ] C1 fresh acceptance at `c8dce251` pending; prior 9 regressions and 2 LiveGui checks passed at `e7f541a4`. C2 second-project adoption and migration docs landed. <!-- t:6k s=in-progress blocked_by=yj,0w -->
- [ ] Measure one-week pilot on comparable workloads, including reconciler tokens/day and outcomes, before widening rollout. <!-- t:z9 blocked_by=c6 -->
- [x] Phase 0 shared contract and nine child cards; docs/TREES-AND-LANDING.md, 07edfab3/fa45e40f. <!-- t:vw -->
- [x] A3 project config, host admission, atomic runnable-main releases and running-release pinning; `423d4833`, `c692bf22` (44 initial targeted tests; final verifier pending). <!-- t:n6 card=ASQ4 -->
- [x] B1 coordinator, accepted policy, Board publication, CLI, transition guards, runnable-main updates and durable split-poll handoff/Board snapshot fix `c8dce251` (fresh acceptance pending). <!-- t:er card=AMQQ -->

## Done means
- Native and guest development panes own separate workspaces; same-file edits never enter another pane's submission, and landing needs one durable submit rather than hunk attribution.
- One coordinator publishes code and Board metadata. Every code publication has a receipt tying the landed SHA to its passing required gate and policy; tip movement triggers new verification.
- Crashes, duplicate submissions, cancellation and pane death lose neither queued jobs nor dirty or clean-unlanded work; restart and rollback are demonstrated under a bounded host resource budget.
- A second Git project uses the same lifecycle and queue through project configuration alone; no-config/ungated operation is never presented as verified.
- First milestone is a verified cutover-ready system: native/guest and Live behavior pass staged review at the final SHA, and an isolated cutover/rollback rehearsal preserves work. Production activation and a measured pilot are subsequent work. Lost work, foreign edits or publication without required evidence fails acceptance.

## Planning notes
### Orchestration: building #3MH4 with subagents (owner, 2026-09-26: "forget the experiment, lets build it with subagents")

**Constraints.** Relay subagents see only their prompt; at most 4 run at once; they cannot start subagents. They still work in the shared checkout and land through legacy `land.py` (the thing being replaced), so the split is by **file ownership**: each agent owns a disjoint file list, mostly new files, and stops and reports if it needs a file outside that list. The coordinator (this pane) writes the contract, reviews each report and diff, reruns its tests, owns contract changes, and moves between phases.

**Phase 0: the contract (coordinator, before any subagent).** `docs/TREES-AND-LANDING.md` as a spec. It covers: the SQLite schema for the registry and queue (WAL, under `$XDG_STATE_HOME/relay/integration/`); the queue state machine and receipt format; Python signatures of every module boundary below; CLI verbs and exit codes for `relay-tree`/`relay-land`; the `.relay/project.toml` schema v1; the new protocol events (`tree_status`, `queue_status`, `main_moved`); the temp-repo test harness conventions (`tests/integration_fixtures.py`); and the file-ownership map. Landed before phase 1 starts. Everything later codes against it.

**Phase 1: four agents in parallel, new files only (no shared file).**
| agent | owns | model/effort | done when |
|---|---|---|---|
| A1 trees | `backend/relay_core/trees.py`, `scripts/relay-tree`, `tests/test_trees.py` | main, high | create (non-cone sparse excludes, init hook), lease, list, explicit sync, remove, retention/GC with receipts, registry; temp-repo tests: two same-file writers, dirty + clean-unlanded recovery, relocation refusal, init failure, quota refusal |
| A2 queue core | `backend/relay_core/landq.py`, `scripts/relay-land`, `tests/test_landq.py` | high, high | immutable submit (SHA + request id, idempotent), state machine, candidate built in a service workspace, CAS publish of the exact verified commit, intent persisted before publish, restart recovery, cancel races, conflict diagnostics, metadata jobs; tests crash at every state boundary and run two competing publishers |
| A3 config + resources | `backend/relay_core/projectconf.py`, `backend/relay_core/integration_slots.py`, `tests/test_projectconf.py`, `tests/test_integration_slots.py` | main, high | project.toml v1 parse/validate/detect (CMake, pytest, npm, cargo, go), gate policy that a config change cannot weaken itself, host-wide admission from MemTotal/disk with headroom and a stated wait reason, fair across repos; warm tip build + `run/<sha>` + `current` symlink (the owner's runnable main) |
| A4 reconciler | `backend/relay_core/reconcile.py`, `tests/test_reconcile.py` | high, high | inputs base/ours/theirs + both cards' intent + conflict or error; model call through the existing provider layer at high effort; per-case token cap + daily budget; refuses results that weaken assertions or drop a side; returns a patch + trailer; tests with a fake model |

**Phase 2: wiring, three agents, disjoint existing files.**
| agent | owns | depends on |
|---|---|---|
| B1 coordinator daemon | `landq.py` (from A2), `backend/relay_core/integration_service.py` (new), `scripts/land.py` `cmd_board_sync` only, `backend/relay_core/agent.py` board-sync spawn region only | A1–A4 |
| B2 backend panes | `board_tools.py` board-root resolution, `tools.py` `command_env`/auto-begin, `guest_launch.py`, `scratch.py`, `worker.py` workspace handling, `docs/AGENT-SESSIONS-PROTOCOL.md` new section | A1, contract events |
| B3 GUI | `src/PaneRuntime.cpp` launch cwd, `src/Pane.h` tree/queue status line, `src/BoardPane.{h,cpp}` Live rows (tree, job, queue reason, recoverable work), "Relay (main)" launcher entry, `tests/boardpane_test.cpp` | contract events (not B2's code) |
B1 goes first. B2 and B3 run in parallel, since their files do not overlap and they meet only at the protocol in the contract.

**Phase 3: independent verification + pilot (two agents).** C1 verifier, on a different model family from the implementers: every scenario in `### Verify`, end to end in temp repos, plus a native + guest two-pane run under Xvfb with an isolated `XDG_CONFIG_HOME`. It files bugs as child cards, and the owning agent's successor fixes them. C2 pilot: a small Python repo adopts the system with only `.relay/project.toml`, plus final `docs/TREES-AND-LANDING.md`, `docs/BUILDING.md`, and a *draft* `CLAUDE.md`/`RELAY.md` landing chapter (not activated).

**Phase 4: first-milestone verification, then a separate production cutover.** Task c6 first verifies the final implementation and rehearses migration/rollback in isolated state. Actual checkout transition and task z9's weeklong pilot follow separately; neither is part of this first milestone.

**Rules every subagent prompt carries.** Own only your file list; land your own paths through `python3 scripts/land.py begin/commit` with `--wait-seconds 900`, never piped through `tail`; targeted tests only, never the full suites; never commit, stash or revert anything you did not write; fix clear gaps rather than listing them; code against the contract, and propose contract changes in the report instead of making them; the report lists files, commits, tests run with results, deviations and open questions.
Implementation kicked off at the owner's instruction: "we're ready. lets go ahead with implementation with the suggested subagents". Contract landed as 07edfab3 in docs/TREES-AND-LANDING.md and supersedes outdated restrictions in the earlier Plan (fixed reconciler model, replay, human acknowledgement, and unapproved-decision language). Approved child workstreams: A1 #RT3B, A2 #FW1C, A3 #ASQ4, A4 #P9ZA, B1 #AMQQ, B2 #80X1, B3 #DV5Y, C1 #8J0A, C2 #2DP8. Four Phase 1 agents started; high role for A2/A4, main for A1/A3. Production remains legacy until verified controlled cutover. Runnable-main costs will be measured, not assumed cache hits.

## Execution Summary
Foundations (#RT3B, #FW1C, #P9ZA), A3 project gates/releases (#ASQ4), B1 coordinator (#AMQQ), B2 native/guest routing (#80X1), B3 GUI (#DV5Y), and C2 second-project adoption (#2DP8) are landed. A3's running-release pinning landed as `c692bf22`; docs followed in `d95a1144`. The parent fixed the quota-interrupted B2/B3 integration in `a0141e41` and `e7f541a4`. C1's `9873de34` records 9 regressions and 2 LiveGui passes at `e7f541a4`; B1's later `c8dce251` fixes split-poll handoff identity and synchronous durable Board snapshots, and awaits fresh final acceptance. The first milestone remains pending that result and an isolated cutover/rollback rehearsal. No production activation, real-model reconciliation acceptance or production throughput measurement has occurred. The actual Relay repository remains in legacy mode.
