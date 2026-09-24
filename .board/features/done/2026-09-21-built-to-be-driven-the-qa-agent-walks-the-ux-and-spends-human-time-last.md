---
id: 74Y5
type: work
status: done
labels: [feature, switchboard, qa]
component: [gui, worker]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
verified_by: openai/gpt-6-astra via codex
parent: YZ8G
rank: zzzzzzzzzzzzzzzze
created: '2026-09-21'
source: owner, 2026-09-21
links: {plans: [], commits: [ece752ef33a41f3cb2fc1b3ee90a89c9c67863c5, e0442a224fe01bdf208821f157be061d5a0a1907], evidence: [docs/qa_evidence/2026-09-21-verify-74Y5-a1/, docs/qa_evidence/2026-09-21-74Y5-named-drive/], related: [YZ8G, WC3E, JNYN], github: null}
---
# Built to be driven: the QA agent walks each step of the user experience, and apps expose a way to be driven by name

## Issue
how do i test out the new QA approach. is the agent going to design simulation? i think the QA agent should also always think about each step of the user experience , and apps should be built for live simulated drives by agents and humans -- the QA agent tries to optimize this to conserve scarce human tester time

## Done means
- The Verify brief (#WC3E) and the Try it brief (#JNYN) state the objective in one sentence: human tester time is the scarce resource; the agent plays everything it can and hands the person only the steps that need a person's judgement, with the minutes it will take stated.
- Before staging, the verifier writes the user's path through the change as numbered steps and marks each one `check` (a test or assertion decides), `agent` (the agent plays it and captures it), or `person` (only a person can judge); the record shows the three counts; a `person` step needs one line saying why an agent cannot judge it.
- Relay can be driven by name, not by pixel: a documented automation seam (the existing action registry, `app_action_run`, and the pane model) lets a driver open a card by id, press a named control on the card page or the tool row, read the notice line and a card's rendered sections, and type into a named box; `docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/ai-pass.sh` is rewritten to use it and stops carrying coordinates.
- A one-page design rule in the docs, for anything built with Relay: name every control, expose an open/press/read seam, keep fixtures disposable; the Verify brief points at it when the card is about an app.
Failure would show as: a brief asking a person to run tests or attach evidence; a driver that misses a click when a layout shifts; a verifier that stages before it has listed the user's steps.

## Plan
**Goal:** Let QA drivers exercise the real Board by stable names and reserve human time for judgement.

**Findings:** BoardView already exposes card selection and Qt control object names; Relay's private local socket opens files, while executeAppCommand uses the existing action registry and pane model. Verify wording is generated in board_tools.py; Try it has a versioned brief.

**Steps:**
1. Add a private socket driver routing open/action through existing app commands, and press/read/type through a bounded BoardView interface with explicit refusal for unavailable controls.
2. Document the seam and add a command-line client; replace the coordinate-based tooling scenario driver.
3. Update Verify/Try it briefs with numbered check/agent/person paths, counts, reasons and human minutes.
4. Test refusal and dispatch behavior and drive a disposable GUI fixture by name.

**Risks:** Shared headers have other sessions' edits; land only this card's hunks. Named driver must never type into terminal or agent-console widgets, and sealed expectations remain sealed.

**Verify:** Targeted Board/app command/brief tests, app build, and isolated Xvfb open/press/read/type scenario with evidence.

## Execution Summary
Landed ece752ef33a41f3cb2fc1b3ee90a89c9c67863c5: a named local socket driver (`scripts/relay-drive`), Board control open/press/read/type with explicit refusals, action-registry and pane routing, and named performance targets. Verify and Try it now require the numbered check/agent/person path, counts, reasons and human minutes before staging. `docs/DRIVING-APPS.md` documents the seam. The tooling fixture uses names throughout; all three isolated live phases reached their rendered outcomes. Evidence: docs/qa_evidence/2026-09-21-74Y5-named-drive/. Independent verification is running separately.
Session handoff, 2026-09-21: implementation ece752ef, implementer captures ab380302, independent verifier evidence e0442a22 and PASS record c0f39021 are landed. Exact clean GUI/backend passed 19 named open/press/read/type operations; three targeted suites passed. See docs/qa_evidence/2026-09-21-verify-74Y5-a1/report.md. Named tooling scenarios are in docs/qa_evidence/2026-09-21-74Y5-named-drive/. No remaining implementation work; independent Verdict permits closure.

## Tests
`ctest --test-dir build -R '^(board|boardpane)$' --output-on-failure`
manual: docs/qa_evidence/2026-09-21-74Y5-named-drive/README.md

## QA checklist
Independent verifier Codex a1, 2026-09-21 local / 2026-09-22 UTC.
- Done means 1 — PASS: both Verify (BoardModel.cpp:666) and Try it briefs explicitly conserve human time and require minutes.
- Done means 2 — PASS: both require numbered check/agent/person steps, counts and reasons; this independent run wrote plan.md before staging (3 check, 3 agent, 0 person, 0 human minutes).
- Done means 3 — PASS: actual isolated GUI completed 19 named socket operations: open card, read rendered sections, Check press and resulting finding, filter type/read-back, notice read, pane model. Missing/hidden/console-input targets refused. Scenario ai-pass.sh reviewed: uses named driver, no coordinate input.
- Done means 4 — PASS: docs/DRIVING-APPS.md covers naming/seam/disposable fixtures; Verify links it.
- Tests ctest board/boardpane — PASS, plus appcommands (3/3 total); build used scripts/relay-build. Unit tests use shared checkout; live drive uses exact clean gate.
- Tests manual named-drive README — independently checked required open/press/read/type path in own fixture; did not reuse implementer captures or rerun all three tooling phases.
Tested GUI/backend revision ece752ef33a41f3cb2fc1b3ee90a89c9c67863c5; 391 scoped gate manifest entries matched.
Fresh evidence: docs/qa_evidence/2026-09-21-verify-74Y5-a1/report.md, transcript.json, ui.png, tests.txt; commit e0442a224fe01bdf208821f157be061d5a0a1907.

## Verdict
2026-09-21 local / 2026-09-22 UTC — **PASS**, independently driven on exact ece752ef GUI/backend. No observed remaining failure in named-control seam or required brief/document instructions. No person judgement claimed. Ready for parent to close; no implementation changes by verifier.
