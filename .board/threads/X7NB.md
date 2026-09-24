<!-- relay:entry 20260920T161034Z-d1 author=claude-code kind=decision -->
### Claude Code · 2026-09-20 16:10
Owner, 2026-09-20, after being shown that plan cards have format and storage but no runtime
behaviour: **"drop plan cards."**

Checked before filing: there are **zero** `type: plan` cards on this board, so nothing is orphaned.
The single card in `issues/planning/` is `type: work` — `planning` is a tab name as well as the
plan type's folder, which is part of what made the type confusing.

<!-- relay:entry 20260920T162818Z-7e author=claude-opus-5 kind=progress -->
Implemented. `plan` is out of `CARD_TYPES`, and with it `PLAN_STATUS_FOLDER`, `PLAN_FOLDER`,
`PLAN_FIELDS`, the `TASK_HEADING` entry, the `expected_folder` / `tab_of` / `_status_from_folder` /
`card_paths` branches, the three `B.PLAN_FOLDER` branches in `board_tools.py`, the `type="plan"`
tag on the `planning` tab in `BoardModel.cpp`, `forge_sync._plan_links` with its three call sites
and the "Relay plan cards (not synced)" footer, and the doc lines (protocol, GITHUB-SYNC,
SWITCHBOARD-FORMAT, SWITCHBOARD-DESIGN, ARCHITECTURE, TASKS-AND-MEMORY-DESIGN, the generated
POLICY.md bullet).

Kept, deliberately: the `## Plan` section and `stage_advance`'s `plan-written`; the Plan turn mode
(`CARD_MODES`, `CARD_MODE_BOARD_TOOLS["plan"]`, `PLAN_HEADING`, the mode plumbing in the pane); the
`plan` thread-entry kind; the `planning/` tab, which holds work cards; `<root>/.relay/plans`; and
`links.plans`, which stays in the schema and is now inert.

`BoardPane::defaultCategory` kept the `planning` skip the dropped tag used to give it, written
there by name with the history in a comment. #W3KD's missing `alias` branch was the same two
lines, so it is fixed and tested here.

Evidence: docs/qa_evidence/2026-09-20-drop-plan-cards/. Moved to needs-verification.

<!-- relay:entry 20260920T163815Z-f1 author=claude-code kind=note -->
### Claude Code · 2026-09-20 16:38
Removed a hand-typed `implemented_by: anthropic/claude-opus-5` from the front matter before
landing. Policy rule 5 and the `board_update_card` appendix both say never to type it — Relay
stamps it, and a value typed by hand is what makes the audit trail a lie. The implementer was an
Opus 5 subagent working the files directly, so nothing stamps it here; its own progress entry above
is the record instead.
