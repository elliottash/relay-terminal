---
id: WBFM
type: work
status: needs-verification
labels: [feature, models, ui, design]
assignee: agent
implemented_by: openai/gpt-6-luna via codex
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: Relay pane, 2026-09-25 (after the question about "agent turns" showing glm-5.3 under runs on but "pane" under override)
links: {plans: [], commits: [2db966438ab8, b3ace8921408, 7f2e2b22e713, bf6d99c50a02, ff7c872db2e6], evidence: [docs/qa_evidence/2026-09-25-wbfm-c1/, docs/qa_evidence/2026-09-25-wbfm-c2/, docs/qa_evidence/2026-09-25-wbfm-b/], related: [00G1, WK7C, N4PW], github: null}
---
# Models pane polish: pick-order headers and ranks, effort spacing, jobs tab as global rules, sources cleanups

## Issue
Three improvements to the Models pane, to come back to. (1) Pick order: make the high / main / flash tier rows real section headers, and make the rank numbers easier to read. (2) Effort tab: its spacing is too loose, so tighten it. (3) Jobs tab: the owner says agent jobs are NOT pane-specific. The tab should set global routing rules per job. Right now "runs on" reports the served pane's live model (`model_roles.roles`, protocol 13) and `agent turns` can't be edited (`settable=false`, `src/JobsTab.cpp:204`; override text "each agent pane's active model"). So the tab reads as tied to one pane, and its main row can't be changed there.

> file this as a card that i will come back to, improving the models pane. 
>
> imrpove the look of the pick order -- for example making high / main  flash proper section headers. and making the ranking numbers more intuitive. 
>
> improve the effort tab, its spaced out too much 
>
> agent jobs is NOT pane specific. it should be for setting rules. so that needs to be revamped
> — elliott · [session:6173581165764472b6c340786cfdcc26](relay://session/6173581165764472b6c340786cfdcc26) · 2026-09-25

## Planning notes
Workstream proposal (2026-09-25, from the pane-and-card review). Three tracks:

- **Track A — verification sweep.** ~20 models-labeled cards sit in needs-verification with landed code and no verifier: N4PW, 4BPE, RND7, PK5Q, MH7P, 7KPN, BXMS, P3KD, H7DN, K7MP, QHEE, BMS1, HJ1T, AVR8, CDP7, MPA2, VPR7, plus bug cards PMX7, PBKR, PH9G, EFT9. One session walks them, verifies, closes or bounces back with evidence. HJ1T's verdict matters doubly: #00G1 was reopened 2026-09-24 for "laggy clicks, controls flipping back", and HJ1T is the lag fix.
- **Track B — Agent jobs becomes global rules** (this card, item 3), done together with #WK7C (agent-settable `roles.<job>` rows): both touch JobsTab + rolestore, and WK7C's rows are the agent-facing half of "jobs are rules". Owns `src/JobsTab.*`, `src/RelayWindowModels.cpp`. Also answers #2CDS if its question is settled by the redesign.
- **Track C — Pick order and Effort polish** (this card, items 1–2): tier rows as real section headers, clearer rank numbers, tighter Effort spacing. Owns `src/ModelPicker*` priorities/effort code. Folds in #9ACN (fifth tab overflows narrow tab bar) if it touches the same files.

#00G1 stays the umbrella: it closes last, once Tracks B and C land and Track A settles the lag report. B and C touch different files and can run in parallel; both coordinate claims if either needs `src/ModelsPane.cpp`. The 2026-09-23 execution split on #00G1 already proved the pattern: parallel subagents on disjoint files, sequential within ModelPicker.

Track C picks up two more removals from the same review (2026-09-25, thread notes): drop the "x of n enabled…" step-2 link under each source and the guest "when it wants to use a tool" permissions row. Both live in the provider-page block of `src/RelayWindowModels.cpp`; the permissions change also touches `Pane::takeGuestRequest` (stop staging/reading `guests/<cli>/permissions`, worker default is already bypass) and removes the stored setting.

## Done means
- Pick-order tab: high / main / flash render as real section headers; rank numbers read intuitively (what "1" means is obvious).
- Effort tab: spacing tightened to match the density of the other tabs.
- Jobs tab: edits global per-job routing rules; it no longer reads as a report about one served pane. The `agent turns` row's pane-following behaviour is presented as such, not as a dead override.
- Sources tab: no "x of n enabled…" row under any provider; no "when it wants to use a tool" row. Guest agents always run with bypass permissions (worker default), with no leftover `guests/<cli>/permissions` staging in `Pane::takeGuestRequest`.
- Failure looks like: any removed string still in the binary, guests losing bypass, or the jobs tab still showing the served pane's live model as if it were the setting.

## Tasks

- [x] Track C1: remove “x of n enabled…” link rows and guest permissions row <!-- t:fa -->
- [x] Track C2a: pick-order tier rows as section headers + intuitive rank numbers <!-- t:cz -->
- [x] Track C2b: effort tab spacing tightened <!-- t:wj -->
- [x] Track B: jobs tab becomes global per-job rules editor, with #WK7C agent-settable rows <!-- t:bt -->
- [x] Track A: verification sweep of needs-verification models cards <!-- t:4v -->

## Plan
**Goal.** Deliver all five requests on this card plus the verification backlog, via subagents on disjoint file claims.

**Wave 1 (parallel, 3 background agents):**
1. *C1 — Sources removals* (land session `wbfm-c1`). Claims `src/RelayWindowModels.cpp`, `src/Pane.h` (hunk-local: `takeGuestRequest` ~12238–12259). Remove the step-2 "x of n enabled…" link row (was ~478) and the guest "when it wants to use a tool" choiceRow (was ~490–520); stop staging/reading `guests/<cli>/permissions`; clear any stored value.
2. *C2 — Picker/effort polish* (land session `wbfm-c2`). Claims `src/ModelPicker.{cpp,h}`; `src/ModelsPane.{cpp,h}` only if needed. Tier rows as section headers, clearer rank numbers, tighter effort-tier spacing. Both pick order (classesTier) and effort (effortTier) render through ModelPicker; tabs wired in ModelsPane.cpp:171–178, 294–295, 370.
3. *A — Verification sweep* (land session `wbfm-a`, no code changes). Walk the 21 needs-verification models cards from ## Tasks; per card run its ## Tests, stage GUI checks under Xvfb with isolated XDG_CONFIG_HOME, write ## QA checklist + evidence under docs/qa_evidence/2026-09-25-verify-<ID>/, move to QA lane or bounce back. #HJ1T first (settles #00G1's lag report).

**Wave 2 (after C1 lands, 1 agent):**
4. *B — Jobs as global rules + #WK7C* (land session `wbfm-b`). Claims `src/JobsTab.{cpp,h}`, `src/RelayWindowModels.cpp` (free once C1 lands), rolestore touch points. Jobs tab edits global per-job rules; "runs on" becomes clearly informational or is replaced by the rule view; #WK7C agent-settable `roles.<job>` rows land in the same pass.

**Risks.** `src/Pane.h` is hot — C1's edit is hunk-local, ~25 lines; C1 must keep the diff minimal and commit fast. Track A verifies other sessions' landed work, touches no source files. B's design has one owner-visible choice (how "runs on" is presented); agent recommends keeping it as a small read-only "live" line per row.

**Verify.** `land.py try` per track with targeted ctest; screenshots under Xvfb with isolated XDG_CONFIG_HOME into docs/qa_evidence/2026-09-25-wbfm-<track>/; this card to needs-verification when all tracks land.

## Tests
- `ctest -R '^modelpicker$'`
- `ctest -R '^modelspane$'`
- `ctest -R '^jobstab$'`
- `ctest -R '^appcommands$'`
- `ctest -R '^settings$'

## Execution Summary
All five workstream tasks are landed. Track B commit: `ff7c872db2e690d7fca87709b93b1fef1e4d9018`; its exact-tree build passed. `ctest:modelpicker`, `ctest:jobstab`, and `ctest:appcommands` passed. `ctest:modelspane` and `ctest:settings` fail only on #SYTR's stale “Helper Agent (Alt+Q)” expectations; the updated narrow “Rules” assertion and the Jobs tab screenshot fixture passed. Evidence: `docs/qa_evidence/2026-09-25-wbfm-c1/`, `docs/qa_evidence/2026-09-25-wbfm-c2/`, and `docs/qa_evidence/2026-09-25-wbfm-b/`.
