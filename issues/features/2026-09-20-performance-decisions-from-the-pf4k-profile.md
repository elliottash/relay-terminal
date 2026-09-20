---
id: GMCF
type: work
status: needs-verification
labels: [feature, performance]
assignee: claude-code
rank: m8
created: '2026-09-20'
source: 'Claude Code in the owner''s terminal, 2026-09-20'
links: {plans: [], commits: [dc091a86, fc2a0683, 692bbfad, d46c4f56, 2f55b0fd, 28f520df, d45effd9, 87e58db1, 4a7bec12, 477e55de, 44a072d4, 91aeec29, 9f650d76, e670b15a, 23361bf8, 1aa2b898, 19f22e3d, f960b3e0, f1778621, 0b53d6f1], evidence: [docs/qa_evidence/2026-09-20-perf-fixes/, docs/qa_evidence/2026-09-20-perf-fixes/prompt-distillation/PROPOSAL.md], related: [PF4K, TZWF, 7M6E, 057J], github: null}
---
# Five performance decisions from the #PF4K profile

## Issue
i want all of these fixes.

explain the decisions and your recs

## Context
The seven fix cards from #PF4K are being implemented (owner: "i want all of these fixes"). These five
items in `docs/qa_evidence/2026-09-20-perf-profile/REPORT.md` ("For the owner to decide") are not
faults with one right answer; each trades something. The questions and recommendations are in the
thread.

## Decisions
Owner, 2026-09-20, answering the five questions in the thread by number:

1. Worker per pane, started lazily: "1 ok. 220MB seems like a good trade for the isolation"
2. Slimmer system prompt: "2 yes, and you can also deploy a fable subagent to propose further pruning / distillation of the system prompt components. or have a short version for local models / short context windows"
3. Keep the session file format, coalesce the saves: "3 ok"
4. Qt6 CI job now, keep shipping Qt6 on 26.04, flip CMake AUTO after #7M6E is verified on Qt6: "4 ok"
5. Asynchronous `systemd-run` probe: "5 yes"

And: "if there are other useful findings or fixes or tweaks, go ahead and do them"

## Result
All five decisions are closed, and the nine distillation decisions the owner approved on top of
decision 2 ("1-9 all seem good to me") are landed. Evidence per item under
`docs/qa_evidence/2026-09-20-perf-fixes/` (`spawn/`, `prompt/`, `prompt-distillation/`, `saves/`,
`keybind/`, `distil/`, `boardpolicy/`, `assembly/`, `cachetok/`); every number below is from spark.

1. Workers stay one per pane (owner: "yes, i want workers per pane"); the lazy-start mapping
   (`spawn/LAZY-WORKER-MAPPING.md`) records why a lazy start would not have saved memory.
2. The prompt: skills catalogue one trigger line each with all 60 covered (was 25), byte-stable
   prefix; then the nine: `set_keybinding` 9,960 → 825 B; `SYSTEM` 5,019 → 2,831 B with no rule
   deleted and each moved rule pinned by `MovedRuleTests`; a subagent `SYSTEM` (10.7 → 3.5 KB);
   assembly most-stable-first and one tool list across modes (a mode switch re-prefills 363 tokens,
   not 11,714); cached-token counts on the usage event, in Activity and the ⓘ pane; todo rules and
   `update_todos`/`ask_user` −1,056 B; a `prompt_profile: auto|full|short` (Options › Agent) with the
   short profile on the Local tier: 1,512 tokens / 1.95 s cold on `local:bonsai` against 14,544 /
   18.5 s at the start; the Switchboard policy tiered (1,261 → 738 tokens per board turn); three
   on-demand tool groups behind `load_tools` (8,834 vs 11,043 tokens on a turn that loads nothing).
   `scripts/eval-requests.py` runs keyless (`--stub`, `--preset local:<id>`) and every scenario passes
   before and after.
3. Session format kept; a turn opens its session files 2.6 times, not 5.8; crash-durability tests added.
4. CI's Qt6 job is a Debian 13 + Ubuntu 26.04 matrix. Qt6 stays the 26.04 package's build; CMake
   AUTO is unchanged until #7M6E is verified on Qt6.
5. The `systemd-run` probe is asynchronous: a 5 s probe holds the window 565 ms, not 3.3 s.

## Open for the owner
- Whether the Lite tier should default to the short profile (the Local-tier A/B passed scenarios
  1, 2 and 8 on both profiles).
- Whether the Local tier gets the five-tool board set: it gets none until a scenario shows a local
  model can file a card.
- Whether the Actions-pane helper may rebind keys: its `app_action_list` shows no shortcuts until its
  `configure` carries `keybindings`, which also hands it `set_keybinding`.

## QA checklist
- [ ] Options › Agent shows a "Prompt profile" row (auto / full / short); changing it applies live and the ⓘ pane says "short prompt" on the Local tier <!-- t:g1 -->
- [ ] Two turns on the Local tier: Activity's second per-call line shows `(N cached)`; a provider that reports nothing shows no cache figure <!-- t:g2 -->
- [ ] Switching build → plan and back mid-conversation on `local:bonsai` does not re-prefill (the ⓘ pane's cached count stays high) <!-- t:g3 -->
- [ ] The agent still acts unasked in the terminal and still sends a destructive command to the prompt box (`scripts/eval-requests.py --stub` scenarios 11 and 12) <!-- t:g4 -->
- [ ] A fresh `.deb` install ships `.pyc` files beside the backend (from #TZWF, same release) <!-- t:g5 -->
- [ ] A fake slow `systemd-run` on PATH: the window appears within a second, the first pane says isolation is unavailable, a later pane is isolated <!-- t:g6 -->
- [ ] `issues/POLICY.md` still says everything the old one said (`boardpolicy/POLICY.diff` accounts for each removed line) <!-- t:g7 -->
