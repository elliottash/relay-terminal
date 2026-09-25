---
id: FVVY
type: work
status: discussing
labels: [feature, design, switchboard, board, worker]
component: [worker, board, plugins]
waiting_on: owner
rank: zzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
verify: {artifact: system, primary: script, also: [probe, person], human: required, criteria: 'on the pilot project, a person reads one case, one run and one artifact back from the ledgers and finds them true to what happened', sign_off: none, effort: high, stakes: rework, blast: capability}
source: 'owner, Relay conversation, 2026-09-23 and 2026-09-25; #1QKM §4 and §6, report §6 and §7'
links: {plans: [], commits: [], evidence: [], related: [1QKM, P2W8, MEPR, 95VZ, 3KB7], github: null}
---
# Name the other objects: counterparty on cards and cases, a run and artifact ledger with provenance, schedules tied to cards, and a non-software pilot

## Issue
give me dossiers on these projects, and form deeper and broader principles that underlie the various types of knowledge work, and the dimensions that an app like relay terminal could support. [...] analyze these issues and add comprehensive plans to cards. clarify anything with me with questions.

## Plan
**Goal.** Name the remaining objects of #1QKM §4 in the data model — as fields and ledgers cards, cases and servers can point at, not as panes — and prove the model on a project that is not software, as the report recommends (§7: Referee-Work for cases, skills, privacy and levels; outcome_test or soundmatch for runs, provenance and decisions). Three objects are worth naming now because a case already needs them: **people / counterparty**, **runs and artifacts**, **schedules**. The rest (workspace, evidence, knowledge, policy, host) already have a home (#P2W8, #BX7B, memory cards, #3KB7, SSH hosts) and stay implicit until a case demands more.

**Findings.**
- Cards have `links: {plans, commits, evidence, related, github}` and no counterparty; case rows have `input` (a path or reference) and `confidential` (drops `input`) and no counterparty either. A referee case is a venue and a deadline; a client case is a person — today both go into the title.
- Nothing records a run: `run_command` and the task-plugin runners (#MEPR) execute and return output; Activity records turns. Artifacts: git for code, `docs/qa_evidence/` for screenshots, nothing for a trained model, a figure or a PDF (report §3.3: soundmatch results without a run id, 549 AIComm PDFs without a commit, outcome_test figures overwritten).
- Schedules: `/loop`, cron and `app_reminder` exist; none is tied to a card, none is listed as a set.
- The pilot projects are the owner's own trees outside this repository (`~/admin/Referee-Work/`, `~/repos/outcome_test/`, `~/repos/soundmatch/`); installing a board there is a decision of the owner's, not this session's.

**Steps.**
1. **Counterparty.** `counterparty:` on work cards (front matter, free text or the id of a memory card of kind `contact`) and `counterparty` on case rows (a reference, dropped like `input` when confidential). `board_list {counterparty: …}` filters; `board_case` accepts it; the row shape in protocol 19.22 grows by one key. Nothing drawn: the card page shows the field where `milestone` shows.
2. **Runs.** `runs.jsonl` beside `cases.jsonl` with the same append-only, locked, size-capped discipline (`relay_core.runs`): `{id, when, host, cwd, command_hash, code_hash (git HEAD when in a repo), config (a path), cost {seconds, tokens, currency}, outputs: [{path, sha256, bytes}], card, case, exit}`. Written by the task-plugin runner (#MEPR) for every kernel or TeX run it owns, and by `run_command` only when a card is claimed on the pane (so a scratch `ls` is not a run). `board_list {runs: true, card: …}` reads it back; a card's `links.runs` is filled by the worker at landing from rows naming the card.
3. **Artifacts.** No third ledger: an artifact is an `outputs` entry of a run, addressed as `run:<id>#<path>`; `links.artifacts` on a card lists those addresses; `superseded_by` is set on the newer run's entry when a later run of the same command rewrites the same path (outcome_test's overwritten figures become a chain, not a loss). Location is the run's `host` + path — #P2W8's workspace object supplies the host.
4. **Schedules.** A `schedule:` field on work cards (`cron` expression or `every <n><unit>`) that `/loop` and `CronCreate` write when started from a card's console, and `board_list {schedules: true}` lists; each firing writes a case row with `served_by` the model signature and `card` the card. Nothing else drawn.
5. **Pilot.** With the owner's go: scaffold a board in Referee-Work (`relay-board.py init`), add `profile:` to its referee skills (`referee-review-support` first: `confidential: yes`, `human: required`, `artifact: text`, `primary: level`), record five recent reports by hand with `board_case` (server `person` or the skill, `counterparty` the journal, no inputs), and read the registry rows (#9FX8) and stats back. Second pilot on outcome_test or soundmatch after step 2: one real run through the Python plugin, its row and its outputs. Findings go on this card as `## Discussion points`; anything that needed a code change files its own card.
6. **Docs.** Protocol 19.22 (rows), a new 19.23 (runs), `docs/BOARD-DESIGN.md` object table, `docs/ARCHITECTURE.md` one paragraph: card → case → server → run → artifact → verdict, the chain from report §6.

**Risks.**
- Scope: four objects and a pilot is large; steps 1–4 are each landable alone, and the pilot can start after step 1 with hand-written rows.
- Privacy: counterparty and run outputs can name a patient, a client or a manuscript; the confidential rule (ids only) must cover both new fields, and the pilot board in Referee-Work must not be synced anywhere the manuscripts are not already.
- `run_command` hooking: recording every command on a claimed card could be noisy; the rule in step 2 is a first cut and the pilot tells whether it is right.
- Owner decisions (thread): which pilot first, whether runs belong to #MEPR's plugin card instead, counterparty as text or as a contact memory.

**Verify.** `PYTHONPATH=backend python3 -m unittest tests.test_cases tests.test_runs tests.test_board_tools.ObjectTests`; a Python-plugin run in a staged workspace producing a run row with its output's hash; then the pilot's read-back by a person (`human: required`).

## Done means
- Cards and case rows accept `counterparty`, dropped like `input` when confidential; `board_list` filters on it.
- `runs.jsonl` records every task-plugin run and every `run_command` on a claimed card with host, code hash, cost and hashed outputs; artifacts are addressed as `run:<id>#<path>` and a rewritten path chains to its predecessor.
- A card started on a schedule carries `schedule:` and each firing writes a case row.
- On the pilot project the owner picks, five real cases and one real run read back from the ledgers as true; findings are on this card.
- Failure looks like: a run row for a scratch command on no card, a counterparty or output path on a confidential row, or a pilot board synced somewhere the case material is not.
