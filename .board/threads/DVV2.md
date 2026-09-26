<!-- relay:entry 20260925T011751Z-e9 author=agent kind=event model=claude-opus-5-5 pane=d7900abc turn=ac598e144c144e34a442ba28dd6ed4c3/2f35f6ba247e4f2a9c3045b774427f0a -->
- ✦ agent created this card in Discussing · .board/features/2026-09-24-relay-owns-agent-scratch-a-ledger-for-every-temp.md

<!-- relay:entry 20260925T011825Z-nn author=agent kind=event model=claude-opus-5-5 pane=d7900abc turn=ac598e144c144e34a442ba28dd6ed4c3/2f35f6ba247e4f2a9c3045b774427f0a -->
- ✦ agent updated this card · waiting_on: (unset) → owner; replaced `## Planning notes`

<!-- relay:entry 20260925T040216Z-75 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T040216Z-76 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Discussing → Planning · a Plan turn started

<!-- relay:entry 20260925T040351Z-d2 author=agent kind=event model=glm-5.3 pane=switchboard turn=e3aa9a9b20c2173487326667687543d6/88b970133f79410ca923ec60d85dc4d2 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T040413Z-va author=agent kind=event model=glm-5.3 pane=switchboard turn=e3aa9a9b20c2173487326667687543d6/88b970133f79410ca923ec60d85dc4d2 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T040416Z-pw author=agent kind=comment mode=plan model=glm-5.3 turn=e3aa9a9b20c2173487326667687543d6/88b970133f79410ca923ec60d85dc4d2 -->
I'll read the card and its related card, then ground the plan in the code.



`update_todos` is refused in a Plan turn — I'll keep the grounding reads and the two card writes as the work. Continuing with the code reads:





The code grounding is done — I have the key hook points (`scratch.py`, `tools.py` spec list, the `agent.py:4395` prepared-call guard, `GuestBridge.h:284`/`BoardWorker.cpp:115` for environments, `AgentContext.h:306` `turnFinished`, `src/ScratchMonitor.*`, and the bundled `disk-hygiene` skill). I need the card's current hash again to write:

Writing `## Done means` first, then the plan.



**Plan written to #DVV2.** The plan turns the Planning-notes design into 12 concrete steps grounded in the code — the ledger extends the existing `backend/relay_core/scratch.py` (`report`/`check`/`gc` already there from #SZHQ), the `/tmp`/`$HOME` refusal extends the prepared-call guard at `backend/relay_core/agent.py:4395`, `TMPDIR` goes into the four process-spawn sites, and the post-turn sweep hooks `turnFinished`. It splits into a Python and a C++ subagent after the shared paths+ledger step, and it needs four owner decisions, all with recommendations marked: refuse vs redirect (refuse), default scratch lifetime (`session`), `.relay/work/` as the `keep` home, and guest `TMPDIR` + `relay-scratch new` through the bridge (yes to both).

<!-- relay:entry 20260925T040416Z-px author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260925T040901Z-0b author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +1

<!-- relay:entry 20260925T040902Z-pp author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +2

<!-- relay:entry 20260925T040902Z-x0 author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +3

<!-- relay:entry 20260925T042001Z-bx author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Planned → Executing, session c4801fd5

<!-- relay:entry 20260925T042001Z-q1 author=owner kind=progress pane=switchboard pane_token=c4801fd5-8cf2-43b1-9c6e-122052fce8d1 -->
Claimed (c4801fd5) · working on it from a terminal pane

<!-- relay:entry 20260925T043525Z-we author=agent kind=progress model=glm-5.3 pane=c4801fd5 turn=4772fe699eed4029a8c4b938bfe6e1a4/b66709c3ea8d4a68be5b023be8a41296 -->
Steps 1–2 landed in the working tree: `relay::scratchpaths` in `src/AppPaths.h` (`RELAY_SCRATCH_HOME`/`RELAY_TOOLS_HOME`/`RELAY_STATE_HOME` overrides over the XDG defaults, matching `scratch_root()`/`tools_root()`/`ledger_path()`/`keep_root()` in `scratch.py`), and the ledger in `backend/relay_core/scratch.py` — `ScratchLedger` (append-only JSONL, last record per id wins), `new`/`release`/`adopt`/`ledger` CLI verbs, `end_session`, `unledgered_created_since` for the sweep, ledger-aware `report`/`gc` (walk only finds what is not in the ledger; removals are `reclaimed` transitions; `keep` never auto-deleted, `install` stays). Existing `tests/test_scratch.py` 16/16 green. Smoke-tested `new`/`release`/`promote`/`adopt`/`ledger` end to end in a sandbox.

**Incident to own (mine, during smoke-testing):** my second sandbox env dropped `RELAY_SCRATCH_ROOTS`, so `relay-scratch --idle-hours 0 gc --apply` ran against the real default roots and deleted other sessions' scratch before I stopped it (~80 s). Verified afterwards: no live process lost its cwd or files (the in-use checks held; `v-q`'s "deleted" fds are Chrome unlinking its own temp files). Gone: most of the old backlog (≥24 h idle — what the standing gc removes anyway, ~1,800 entries) **plus** ~3.3 GB that was not: `bash-edit-diff` 650 MB (6 min old), sweet-street session 16.6 GB (25 h), `relay-a6sh` 804 MB (25 h), `relay-gw-verify` 484 MB (7 h), `relay-gateway-deploy` 480 MB (7 h), `tmp.yiTpzaRi8D` 482 MB (8 h), `tmp.602qMDmA47` 200 MB (10 h), partial removal of session `f8849095` (3 h), and assorted 3–7 h `relay-*`/`tmp*` dirs. Nothing recoverable (no trash). Fix shipped with this card: `gc --apply` below a 6 h idle floor on the default roots now refuses without `--force-idle` (`MIN_IDLE_HOURS` in `scratch.py`), exactly the "deleting is a ledger transition, never a guess" rule this card is about.

<!-- relay:entry 20260925T044940Z-59 author=owner kind=comment mode=discuss -->
Research if warp / Claude / GitHub / etc have a tool like this and if so how it works

<!-- relay:entry 20260925T045055Z-fp author=agent kind=note model=glm-5.3 pane=switchboard turn=e3aa9a9b20c2173487326667687543d6/c0ca531534384e6782d4667150bf31e1 -->
Research: does anyone else ship "the tool owns agent scratch"? (Repo-grounded where possible; items marked *[memory]* come from training knowledge, not verified live in this turn — a Run turn with the deep-research skill can confirm them.)

**No one has the whole design.** Every neighbour ships exactly one slice of it:

- **Claude Code** *(repo-grounded)*: per-session tool-output dirs under `/tmp/claude-<uid>/` (112 GB observed here) with no ledger, no lifecycle, no ownership record; cleanup is the OS's (tmpfiles/reboot). Relay's own `backend/relay_core/tryit_protocol.py:792` hardcodes `/tmp/claude-{uid}/tryit` as its short-run dir — this card's own Try-it is a customer of the bad pattern. *[memory]* `cleanupPeriodDays` expires chat transcripts under `~/.claude/projects`, but that is history, not scratch.
- **Codex CLI** *(repo-grounded, `guest_harness_codex.py`)*: takes the *confinement* slice only — every write goes through an OS sandbox (`sandbox` + `approvalPolicy` per thread), and a write outside the sandbox is refused with the file named (:1486). No temp dirs of its own to manage, no lifecycle, nothing ledgered.
- **Warp** *(repo-grounded via the `factory-files` skill)*: factory agents don't run on the user's machine at all — they run server-side in Oz sandboxes / runner Docker images, so scratch is the container and dies with the task. That answers "important work in /tmp" by making *nothing* persist (no long builds, no cross-turn state) — a different trade-off, and it can't apply to Relay, whose agents work in the user's real checkout.
- **GitHub** *[memory]*: Actions gives each job a platform-owned `RUNNER_TEMP`, wiped when the job ends — per-job temp with enforced cleanup, but no ledger, no classes, no user-visible record. Copilot's coding agent runs in an ephemeral container per task: same "nothing persists" answer as Warp.
- **Gemini CLI** *[memory]*: the closest single-CLI analogue — it uses `~/.gemini/tmp/<session>` instead of `/tmp` and clears sessions older than ~72 h at startup. Owns the temp dir and the cleanup, but age-only, no ledger, no classes, no enforcement.
- **OS/tool precedents for the rest**: macOS's per-user `$TMPDIR` (per-owner temp, OS cleans after ~3 days idle) is the `scratch`-home precedent; `systemd-tmpfiles` is the age-only cleaner this card replaces for agents; **nix GC roots** are the closest precedent for `keep`/`until-promoted` (nothing is collected while a root references it, collection is a listed transition); bazel/ccache own their caches under XDG cache and GC by size/age — tool-owned scratch, but per-tool and invisible to the user.

**Implication for this card.** The four decisions stand on their own merits rather than against an incumbent: there is no established competitor pattern to copy. The two ideas with real precedent elsewhere are `scratch` under a per-owner cache dir with age cleanup (macOS/Gemini/Actions) and `keep`-as-explicit-root (nix) — both already in the plan. Warp's and GitHub's "make everything ephemeral" answer is structurally unavailable to Relay (agents work in the user's real tree, so a ledger and classes are the only way to get the same discipline). Codex's refusal-with-path-named is exactly the enforcement style step 6 already chose.

<!-- relay:entry 20260925T045102Z-7t author=agent kind=comment mode=discuss model=glm-5.3 turn=e3aa9a9b20c2173487326667687543d6/c0ca531534384e6782d4667150bf31e1 -->
The owner asked for research into how Warp, Claude, GitHub et al. handle this. In a Discuss turn I can't run commands or browse the web, so I'll ground what I can in this repository (it integrates Claude Code and Codex as guest harnesses, and carries the Warp skills) and mark the rest as from memory.

The repo itself is good evidence here: Relay's own integrations document how Claude Code and Codex behave (`scratch.py`, `tryit_protocol.py`, `guest_harness_codex.py`), and the `factory-files` skill documents how Warp's software-factory agents run. I'll record the comparison on the card, then summarize.

**Research recorded on #DVV2** — the short version: nobody ships the whole design; each neighbour has exactly one slice of it.

- **Claude Code** — per-session dirs under `/tmp/claude-<uid>/`, no ledger, no lifecycle; cleanup is the OS's (repo-grounded: 112 GB observed here, and `tryit_protocol.py:792` is Relay's own use of the same bad pattern).
- **Codex** — confinement only: OS sandbox per thread, writes outside it refused with the path named. No temp lifecycle.
- **Warp** — factory agents run server-side in Oz sandboxes/runner containers, so scratch dies with the task: "nothing persists" as the answer. Structurally unavailable to Relay, whose agents work in the user's real checkout.
- **GitHub** — Actions gives each job a `RUNNER_TEMP` wiped at job end (per-job temp, no ledger); Copilot's agent is ephemeral-container, like Warp.
- **Gemini CLI** *(from memory)* — closest single-CLI analogue: `~/.gemini/tmp/<session>` instead of `/tmp`, sessions older than ~72 h cleared at startup. Age-only cleanup, no ledger or classes.

Precedents that back the plan's choices: macOS per-user `$TMPDIR` (per-owner scratch home + OS cleanup), nix GC roots (the `keep`/`until-promoted` model), bazel/ccache (tool-owned XDG cache with GC), and Codex's refuse-and-name-the-path enforcement. Caveat: this turn has no web access, so the GitHub/Gemini/Claude-settings items are marked *[memory]* in the note on the card — say the word if you want them live-verified, which needs a Run turn with the deep-research skill.

<!-- relay:entry 20260925T050246Z-r5 author=owner kind=comment mode=plan -->
I am not opposed to saving in tmp, as long as it gets tracked and cleaned. Is .relay/tmp better?

<!-- relay:entry 20260925T050422Z-dp author=agent kind=event model=glm-5.3 pane=switchboard turn=e3aa9a9b20c2173487326667687543d6/7d5b032e7fa848c9b952c34d174c7d51 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T050446Z-vf author=agent kind=event model=glm-5.3 pane=switchboard turn=e3aa9a9b20c2173487326667687543d6/7d5b032e7fa848c9b952c34d174c7d51 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T050451Z-rf author=agent kind=comment mode=plan model=glm-5.3 turn=e3aa9a9b20c2173487326667687543d6/7d5b032e7fa848c9b952c34d174c7d51 -->
The owner's question changes a design decision, so this Plan turn revises the card: I'll read its current state (it's mid-execution — steps 1–2 landed), confirm what's in the working tree, then revise `## Done means` and `## Plan` with the `.relay/tmp` question answered.

Now confirming what actually landed in the working tree (the thread says steps 1–2 did), so the revised plan states it as fact:

Both landed pieces confirmed in the tree (`relay::scratchpaths` in `src/AppPaths.h:87`, `ScratchLedger`/`end_session`/`unledgered_created_since`/`MIN_IDLE_HOURS` in `backend/relay_core/scratch.py`). Writing the revised `## Done means`, then the plan with the `.relay/tmp` question answered:



**Plan revised on #DVV2** — steps 1–2 marked landed (verified in the tree: `scratchpaths`, `ScratchLedger`, the 6 h idle floor), and your question answered as decision (v): **no to `.relay/tmp` for scratch — `.relay/` stays the home of `keep`**. In-project scratch gets wiped behind the ledger's back by `git clean -xdf` (the `clean-commit` skill does that), can't live in read-only or project-less panes; `/tmp` proper fails the "tracked and cleaned" test because tmpfiles and reboots are a second deleter no ledger can see. The cache dir is already exactly what you asked for — tracked and cleaned, Relay the only deleter — and `RELAY_SCRATCH_HOME` moves it anywhere (including `/tmp`) per machine if you ever disagree. Nothing else needs you; Run picks up the remaining steps 3–12.

<!-- relay:entry 20260925T051404Z-71 author=agent kind=event model=glm-5.3 pane=c4801fd5 turn=4772fe699eed4029a8c4b938bfe6e1a4/b66709c3ea8d4a68be5b023be8a41296 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["SZHQ"], "github": null… → {"plans": [], "commits": ["0da66145", "35fa497b", "acdbb2d0"], "evidence": ["doc…; appended to `## Tests`

<!-- relay:entry 20260925T051421Z-f2 author=agent kind=event model=glm-5.3 pane=c4801fd5 turn=4772fe699eed4029a8c4b938bfe6e1a4/b66709c3ea8d4a68be5b023be8a41296 -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260925T051427Z-n5 author=agent kind=event model=glm-5.3 pane=c4801fd5 turn=4772fe699eed4029a8c4b938bfe6e1a4/b66709c3ea8d4a68be5b023be8a41296 -->
- ✦ agent moved this card · Executing → Needs verification · Landed on main as 0da66145 + 35fa497b + acdbb2d0: ledger, scratch_dir/scratch_release, TMPDIR, write guard, post-turn sweep, monitor, adopt, skills, docs/SCRATCH.md. Tests and the incident record are on the card; evidence in docs/qa_evidence/2026-09-25-dvv2-scratch-ledger/. · evidence docs/qa_evidence/2026-09-25-dvv2-scratch-ledger/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260925T051437Z-q5 author=agent kind=progress model=glm-5.3 pane=c4801fd5 turn=4772fe699eed4029a8c4b938bfe6e1a4/b66709c3ea8d4a68be5b023be8a41296 -->
Executed and landed. `0da66145` is the main implementation (verified by land.py's slot build of the exact tree): ledger + CLI verbs, `scratch_dir`/`scratch_release` tools, TMPDIR for shell/guests/worker keyed on the pane token, write guard with the exact `scratch_dir` call in the refusal, once-per-turn sweep, ledger-aware report/gc, monitor ledger totals/unpromoted keeps/orphans, `disk-hygiene`+`deliver` rewrites, `docs/SCRATCH.md`. `35fa497b` evidence, `acdbb2d0` follow-up (try-it root under the scratch home — the revised plan's late addition — plus the idle-floor and reconcile regression tests). Tests: `test_scratch_ledger.py` 29 (1 skip), `test_scratch.py` 16/16 unchanged, `ctest -R scratchmonitor` 12/12, full `scripts/relay-build`. Two things to read in `## Execution Summary`: the gc incident I caused (and the floor that now prevents it), and the one deferred item — a first-class `relay-scratch new` bridge verb for guests — because the twelve published bridge tools are a versioned protocol shared with installed guest extensions. `relay-scratch adopt` remains dry-run by design; the owner runs `adopt --apply` and resolves the orphan list (step 10's walk-down).

<!-- relay:entry 20260925T051445Z-y8 author=agent kind=progress model=glm-5.3 pane=c4801fd5 turn=4772fe699eed4029a8c4b938bfe6e1a4/b66709c3ea8d4a68be5b023be8a41296 -->
- ✦ agent is preparing Try it for this card

<!-- relay:entry 20260925T051733Z-7t author=agent kind=event model=glm-5.3 pane=c4801fd5 turn=4772fe699eed4029a8c4b938bfe6e1a4/b66709c3ea8d4a68be5b023be8a41296 -->
- ✦ agent updated this card · appended to `## Try it`

<!-- relay:entry 20260925T051741Z-3k author=agent kind=evidence model=glm-5.3 pane=c4801fd5 turn=4772fe699eed4029a8c4b938bfe6e1a4/b66709c3ea8d4a68be5b023be8a41296 -->
Try-it staged in `docs/qa_evidence/2026-09-25-tryit-DVV2/`: `stage.sh` (rerunnable sandbox of the full scratch lifecycle: TMPDIR row, scratch/keep allocation, release→reclaimed, keep refusing silent deletion then promoting, end_session, the sweep naming a planted rogue `/tmp` dir, and the 6 h gc floor refusing `--idle-hours 0 --apply`), a captured `transcript.txt` (all nine steps exit 0, reclaimed+promoted+refused all shown), `staging-notes.md` (what is staged vs what needs the app), sealed `expected.md`, and `path.md`. Captures: text only — the card is a CLI/backend surface.

<!-- relay:entry 20260925T051752Z-78 author=agent kind=event model=glm-5.3 pane=c4801fd5 turn=4772fe699eed4029a8c4b938bfe6e1a4/b66709c3ea8d4a68be5b023be8a41296 -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["0da66145", "35fa497b", "acdbb2d0"], "evidence": ["doc… → {"commits": ["0da66145", "35fa497b", "acdbb2d0", "275d4e27"], "evidence": ["docs…

<!-- relay:entry 20260925T153009Z-28 author=agent kind=note model=claude-opus-5-5 pane=5ca366c8 turn=b8fda29390ee42089a246c82b635fb08/ef7dcb93001d47b7b20d344f56b11bd6 -->
Heads-up from #H1BS (commit 7ce5c2ef, owner decision): the session-root formula moved while this card is in verification. `scratch.session_root()` and `relay::scratchpaths::sessionRoot()` now name a session by its **first 12 characters** (`_safe_name(session)[:12]` / `session.left(12)`), so a pane's TMPDIR is `<scratch home>/7dbb2c54-921/tmp` and no longer `<scratch home>/<36-char uuid>/tmp`. The reason: the full UUID left no room under the 108-byte Unix socket limit, and Chrome died with "Socket path too long". docs/SCRATCH.md is updated. Ledger rows need no migration because they are keyed by absolute path: a pane gets a fresh short root on its next conversation, and the old row ages out at days:7. Your uncommitted IdleFloorTests hunk in tests/test_scratch_ledger.py was left untouched, in the working tree.

<!-- relay:entry 20260926T010603Z-n5 author=agent kind=event mention=3MH4 model=claude-fable-5-1 pane=f35051fe turn=dbc9af56def4427c91f4fecd2d277da4/5bf51083235d46c8917f0ef9d13390aa -->
mentioned in #3MH4 · 2026-09-26 · agent
