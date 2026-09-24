<!-- relay:entry 20260920T185831Z-c3 author=claude-code kind=question -->
### Claude Code · 2026-09-20 18:58
Five decisions from the #PF4K profile, each with my recommendation.

1. **One Python worker per pane costs ~27 MB each** (73 / 161 / 221 MB at 1 / 4 / 6 panes; idle workers use no CPU). `Pane`'s constructor calls `startWorker()` before the window is mapped (`src/Pane.h:470-472`). Sharing one worker between panes would save ~25 MB per extra pane but gives up the per-pane `MemoryMax` scope and crash isolation, and makes one pane's long tool call everybody's problem. **Recommendation: keep one worker per pane, but start it lazily** — on first focus of the prompt box or first agent use — so a pane used only as a terminal never pays for one. With #TZWF's bytecode fix a worker is ready in ~70 ms, which focus-time spawning hides. Do not share workers.
2. **The system prompt is 25 KB per request, 41 KB with a board** (~6,000 / ~10,000 input tokens before the conversation starts). `SYSTEM` itself is 5 KB; ~20 KB is the list of 60 bundled skills plus todo and app rules; the board adds `board_policy.md` and its header. **Recommendation: do not cut rules; cut the catalogue.** (a) one short line per skill instead of full descriptions, full text on load; (b) confirm the prompt is byte-stable across turns so provider prompt caches and llama.cpp's prefix cache hit — that matters most on the local Bonsai tier, where 10k tokens of prefill is seconds; (c) measure tokens per section first so the cut is aimed. A small card, after the fixes land.
3. **Session files are rewritten whole on every save** (~10 ms on your largest session, 5.8 opens of the file per turn, off the GUI thread, atomic). **Recommendation: keep the format.** 10 ms beside a model call is invisible, the atomic rewrite is what makes a crash safe, and an append-only format would complicate rewind, fork, compaction and every reader (index, phone). Worth doing without a format change: coalesce the ~6 saves per turn into one or two.
4. **Qt6 or Qt5 on Ubuntu 26.04.** The .deb already builds Qt6 there; on sphinxpad Qt6 is 25 % faster to a window and a third lighter, equal elsewhere, slower only in the Switchboard filter (2.5× per key — #7M6E moves that off the GUI thread). But CI builds only Qt5, which is how `tests/wordwrap_test.cpp` stopped compiling on Qt6 unnoticed (fixed, d2923c9b), and CMake AUTO picks Qt5 when both are installed. **Recommendation: (a) add a Qt6 CI job now — no downside; (b) keep shipping Qt6 on 26.04; (c) flip AUTO to prefer Qt6 only after #7M6E is verified on the Qt6 build. spark (24.04, Qt 5.15) stays as it is.**
5. **`isolation::available()` blocks the GUI thread on `systemd-run --user --scope true` for up to 3 s before the first window** (`src/Isolation.h:29`; 0–10 ms when healthy). A degraded user manager means a 3-second launch with nothing on screen. **Recommendation: fix it — start the probe asynchronously as the first thing in `main()`, let the first pane wait for the answer at most ~300 ms, and on timeout start that pane unisolated with the existing "isolation unavailable" notice, re-probing for later panes.** The cost is that in the degraded case the first pane has no memory cap; the alternative is a hang, and the cap is a safety net, not a security boundary.

<!-- relay:entry 20260920T195147Z-c4 author=claude-code kind=decision -->
### Claude Code · 2026-09-20 19:51
The owner decided all five: "1 ok. 220MB seems like a good trade for the isolation" · "2 yes, and you can also deploy a fable subagent to propose further pruning / distillation of the system prompt components. or have a short version for local models / short context windows" · "3 ok" · "4 ok" · "5 yes" · "and if there are other useful findings or fixes or tweaks, go ahead and do them". Card to executing; one implementer per decision, each landing with #GMCF in the message.

<!-- relay:entry 20260920T202036Z-c5 author=claude-code kind=question -->
### Claude Code · 2026-09-20 20:20
Decision 2 follow-up: the Fable distillation proposal is landed (79fd67d6, `docs/qa_evidence/2026-09-20-perf-fixes/prompt-distillation/PROPOSAL.md`). Its measurement corrects the profile: a pane sends **9,823 tokens** of prompt + tools per request without a board and **14,544** with one, and tool schemas are 58–70 % of that — `set_keybinding` alone is 2,592 tokens. On `local:bonsai` a cold prefill is 18.5 s today; 1.9 s with the short profile. Nine decisions, smallest risk first, each recommended yes by the proposal:
1. Slim `set_keybinding` (drop the 91-action listing and enum) — ~2,500 tokens per request. **Being done now** under "other useful fixes or tweaks".
2. Distil `SYSTEM` to `SYSTEM.distilled.txt` — no rule deleted, ssh/program/run_in_terminal detail moves to the tool descriptions that already carry it; ~480 tokens. Needs the owner's read: it is the agent's voice.
3. A subagent `SYSTEM` without the terminal, ssh and rendering lines — ~1,200 tokens per subagent request.
4. Fix the assembly order, move the plan-mode note into the turn's context block, keep the tool list fixed across modes — no bytes saved, but a mode switch stops re-prefilling (13–18 s → 0.2 s on the Local tier) and hosted prompt caches can hit across a session.
5. Show cached-token counts in Activity and `session_info` — how 4 gets verified per provider.
6. Tighten the todo rules and the `update_todos` / `ask_user` descriptions — ~350 tokens.
7. Short profile for the Local tier (`prompt_profile: auto|full|short`, auto = local endpoint or context ≤ 32k) — 14,544 → 1,425 tokens, 18.5 s → ~2 s cold.
8. Tier the Switchboard policy to `BOARD-POLICY.core.txt` — ~780 tokens per board turn. The owner's voice: read the draft first.
9. On-demand tool groups (app, own-session, tests) behind one `load_tools(group)` — ~2,150 tokens on hosted providers only.
Recommendation: 1, 4, 5, 7 first (no wording of yours changes; 7 is what makes the Local tier usable); then 2, 3, 6, 8 after reading the drafts; 9 last.

<!-- relay:entry 20260920T202117Z-c6 author=claude-code kind=decision -->
### Claude Code · 2026-09-20 20:21
Decision 1 closed: the owner, shown the lazy-worker mapping (`docs/qa_evidence/2026-09-20-perf-fixes/spawn/LAZY-WORKER-MAPPING.md`), said "yes, i want workers per pane". Workers stay one per pane, started at pane creation as today; nothing changes. The mapping stays as the record of why a lazy start is not a memory fix here.

<!-- relay:entry 20260920T202624Z-c7 author=claude-code kind=decision -->
### Claude Code · 2026-09-20 20:26
The owner on the nine distillation decisions: "1-9 all seem good to me". Implementers: text (2, 3, 6), board policy (8), assembly then short profile then on-demand tools (4, 7, 9), cached-token counts (5); 1 was already in progress.

<!-- relay:entry 20260920T212011Z-c8 author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 21:20
everything landed: the five decisions and the nine distillation decisions; commits in links.commits, results and the QA checklist in the body, three questions left open for the owner. Moved to needs-verification.

<!-- relay:entry 20260920T213550Z-c9 author=claude-code kind=decision -->
### Claude Code · 2026-09-20 21:35
The owner on the three open questions in the body: "1 yes, 2 yes, 3 yes" — the Lite tier defaults to the short profile; the Local tier gets the five-tool board set; the Actions-pane helper's configure carries the keybindings, so it may rebind keys. Two implementers.

<!-- relay:entry 20260920T215922Z-ca author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 21:59
the three follow-ups landed (cd356730, 497eb591, 7d868c4c, f8becbb1); commits and results on the card. One fault found on the way is #Z4HR's: fdb662d6 added rule 10's card-body schema to board_policy.md after decision 8 tiered it, so `test_the_board_policy_block_stays_tiered` fails on main (3,893 B against a 3,072 B budget, ~520 tokens back on every board turn) — commented on #Z4HR.
