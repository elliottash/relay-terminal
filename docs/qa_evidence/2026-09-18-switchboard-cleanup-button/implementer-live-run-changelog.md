# Switchboard cleanup c-516545

- **Run**: c-516545 (applied)
- **Finished**: 2026-09-18T15:03:03+00:00 after 724.0 s, outcome `done`
- **Model**: glm-5.3
- **Cards**: 129 before, 130 after
- **Writes**: 19
- **Scope**: the whole board

## Changes

| # | Action | Card | Summary | File |
|---|---|---|---|---|
| 1 | merge | #4WHD ← #KH72 | merged 1 card(s) in: #KH72 Model roles in settings: main, fast, terminal-use, subagent, Switchboard, chores, vision | `issues/features/needs_qa_llm/2026-09-17-model-roles.md` |
| 2 | update | #XZZB | labels: ["settings", "gui", "ux"] → ["feature", "settings", "gui", "ux"] | `issues/planning/2026-09-17-improve-the-options-settings-menu.md` |
| 3 | update | #SP4N | labels: ["settings", "gui", "ux", "palette"] → ["feature", "settings", "gui", "ux", "palette"] | `issues/changes/needs_qa_llm/2026-09-18-settings-as-a-full-pane.md` |
| 4 | update | #N3WD | labels: ["change"] → ["change", "bug"] | `issues/changes/needs_qa_llm/2026-09-18-bubbles-take-the-column.md` |
| 5 | update | #H7KP | labels: ["change"] → ["change", "feature"] | `issues/changes/needs_qa_llm/2026-09-18-pane-buttons-and-header-drag.md` |
| 6 | update | #D5MC | labels: ["change"] → ["change", "bug"] | `issues/changes/needs_qa_llm/2026-09-18-one-icon-everywhere.md` |
| 7 | update | #265N | labels: (unset) → ["feature"] | `issues/planning/2026-09-17-cleanup-audit-follow-ups-judgment-call-dead-code.md` |
| 8 | update | #C6YX | labels: ["change"] → ["change", "feature"] | `issues/changes/needs_qa_llm/2026-09-18-model-tier-commands-and-flash-naming.md` |
| 9 | update | #9V1F | labels: ["change"] → ["change", "feature"] | `issues/changes/needs_qa_llm/2026-09-18-queue-items-edit-in-the-prompt-box.md` |
| 10 | update | #AGN8 | assignee: (unset) → agent; implemented_by: (unset) → Claude Opus 5 (Claude Code, GUI D), 2026-09-17 | `issues/features/needs_qa_llm/2026-09-17-agent-queue-steering-and-editing.md` |
| 11 | update | #X5D1 | assignee: (unset) → agent; implemented_by: (unset) → Claude Opus 5 (Claude Code session), 2026-09-17 | `issues/features/needs_qa_llm/2026-09-17-agent-responses-in-terminal.md` |
| 12 | update | #4TZD | assignee: (unset) → agent; implemented_by: (unset) → Claude Opus 5 (Claude Code, GUI D), 2026-09-17 | `issues/features/needs_qa_llm/2026-09-17-queue-shell-commands-while-busy.md` |
| 13 | update | #90JF | assignee: (unset) → agent; implemented_by: (unset) → Claude Opus 5 (Claude Code session), 2026-09-17 | `issues/changes/needs_qa_llm/2026-09-17-pre-submit-run-check.md` |
| 14 | move | #X59Q | Inbox → Ready | `issues/changes/2026-09-18-continue-should-reach-the-agent.md` |
| 15 | move | #0T2R | Inbox → Ready | `issues/changes/2026-09-18-pane-buttons-brighter-outline.md` |
| 16 | move | #ZW95 | Inbox → Ready | `issues/changes/2026-09-18-tab-completion-adds-a-stray-dash.md` |
| 17 | move | #XZZB | Inbox → Ready | `issues/planning/2026-09-17-improve-the-options-settings-menu.md` |
| 18 | comment | #KKYC | note: Cleanup pass 2026-09-18: both paths this card asks about now exist — #0C7V landed single click opening a folder in the e | `issues/features/2026-09-18-clicking-a-folder-opens-or-navigates.md` |
| 19 | create | #F36V | created · Ready | `issues/changes/2026-09-18-card-id-ot32-output-token-limit-is-rejected-by-e.md` |

## Refused

- `board_read`: 'OT32' is not a card id: four Crockford-base32 characters with a letter, e.g. K7Q2.
- `board_read`: no card #0T32 on this board.
- `board_update_card`: #265N changed since you read it; read it again and reapply your change.
- `board_update_card`: #C6YX changed since you read it; read it again and reapply your change.
- `board_update_card`: #K9SR changed since you read it; read it again and reapply your change.
- `board_update_card`: #K9SR changed since you read it; read it again and reapply your change.
- `board_update_card`: #K9SR changed since you read it; read it again and reapply your change.
- `board_update_card`: #K9SR changed since you read it; read it again and reapply your change.

## What the agent said

Survey notes so far: the workspace holds `issues/` plus a single "board snapshot" commit, and `docs/qa_evidence/` does not exist in this snapshot at all — so on-disk evidence checks are vacuous here and git history can't confirm landings. Status↔folder consistency scanned clean. Now reading the cards I may touch, starting with the inbox and the settings cluster.The settings cards are cross-linked analysis vs. implementation — not duplicates. Continuing with the discussing cards, the in-progress card, and their landed counterparts.The discussing cards are genuinely parked on owner answers, and `#W5N2` is real active work — leaving those. Now the model-roles cluster, which looks like the strongest merge candidate.`#KH72` and `#4WHD` are the same request split across a request card and its implementation card — that's a genuine merge. `#NK66` is a later, different UI layer (keys modal, tiers) — leaving it. Now the queue cluster and the label-only cards.`#AGN8`, `#1T0W`, `#9V1F` are three distinct landed pieces (queue UX, steering, queue-editing refinement) — no merge there. Continuing with the remaining label-candidate cards and the no-implementer QA cards.`#OT32` was rejected as an id — the roster line likely renders a leading zero as `O` (like `#0T2R`); trying `0T32`. Also reading the remaining candidates.All reads done — `#QRNJ`/`#308N` (feature vs. its silent-failure fix) and `#K13B`/`#P4GP` (site copy vs. beta release) are related but genuinely distinct, so they stay. One real fault found: the output-token-limit card's id really is `OT32` — a letter the board tools refuse, so it's unaddressable (can't even be relabeled). Now making the writes.Hash conflicts — the board wants a read immediately before each write. Switching to strict read→write pairs, one write per message.The board invalidates any card read that shared a message with a write — switching to strictly alternating read/write messages for the remaining six updates.`#K9SR` refuses updates deterministically (read → write with matching hash, three times) — leaving it and noting that. Continuing with `#9V1F`.All backfills done. Now the status moves for the triaged inbox cards.`#D60R` confirms the click scheme that landed. Leaving the note on `#KKYC` and filing the unaddressable-id fault card.Cleanup complete. Final state verified (inbox empty, merge recorded, labels live).

- **Merged**: `#KH72 + #4WHD → #4WHD` — KH72 was the owner's model-roles request, 4WHD the implementation card it landed as (KH72's own body pointed at 4WHD's file for the implementation notes and QA checklist); KH72's text and the owner's verbatim request are preserved in 4WHD, KH72's file closed as `dropped` pointing there.
- **Split**: none — no card mixed unrelated work (closest candidates, `#05J2` export+optional-sync and `#NK66` keys/roles/settings modals, are each one coherent ask).
- **Status**: `#X59Q`, `#0T2R`, `#ZW95` inbox → ready (clear intake bugs with acceptance lines, nobody working on them); `#XZZB` inbox → ready (analysis delivered in the body, remaining findings specified). Also `#AGN8`, `#X5D1`, `#4TZD`, `#90JF` kept in needs-qa-llm but gained the `implemented_by`/assignee their own bodies already documented, so the QA lane records who implemented them.
- **Labels / ranks**: 9 label fixes — added the missing bug/feature axis to `#XZZB`, `#SP4N` (feature), `#265N` (feature, was unlabeled), `#N3WD`, `#K9SR*, #D5MC` (bug), `#C6YX`, `#H7KP`, `#9V1F` (feature). No rank changes. *`#K9SR` refused every update — the tool returned "changed since you read it" three times with a matching hash, read immediately before each write — so it still says `change` only.
- **Sections**: unchanged.
- **Left alone**:
  - **needs-qa-llm (105 cards)** — correctly parked awaiting QA; closing any needs a verdict from a non-implementer, which I can't supply. Note: this checkout is a snapshot with a single "board snapshot" commit and **no `docs/qa_evidence/` tree**, so on-disk evidence and landed-commit checks were impossible for every card; statuses were judged from each card's own body, thread and front matter.
  - `#W5N2` in-progress — genuinely active (phases landed, "What is left" list with notifications/multiplayer unbuilt).
  - `#8E4Q`, `#KKYC` discussing — open owner questions; I left a note on `#KKYC` that `#0C7V` (single click → explorer) and `#D60R` (right-click → Navigate here) already landed both paths, so the owner may just close it.
  - Not merged, deliberately: `#NK66` vs `#4WHD` (later, different UI layer: keys modal, Main/Flash/Lite tiers); `#QRNJ` vs `#308N` (feature vs. its silent-failure fix); `#K13B` vs `#P4GP` (site copy vs. beta release); `#AGN8`/`#1T0W`/`#9V1F` (three landed queue pieces: UX, steering, editing refinement); `#XZZB` vs `#SP4N` (analysis card with open findings vs. the pane implementation, already cross-linked).
  - **New fault card `#F36V`**: the output-token-limit card's id really is `OT32` — the letter O is outside the board's base32 alphabet, so no board tool can read, move or relabel it (its `change`-only label is unfixable for the same reason). I did not touch that card's file.- **Merged**: `#KH72 + #4WHD → #4WHD` — the owner's model-roles request and the implementation card it landed as were one card in two files (KH72's own body pointed at 4WHD for the implementation notes and QA checklist); KH72's text and the verbatim request are preserved inside 4WHD, its file closed `dropped` pointing there.
- **Split**: none — no card mixed unrelated work.
- **Status**: `#X59Q`, `#0T2R`, `#ZW95` inbox → ready (clear, unstarted intake bugs); `#XZZB` inbox → ready (analysis delivered, remaining findings specified); `#AGN8`, `#X5D1`, `#4TZD`, `#90JF` stay in needs-qa-llm but now carry the `implemented_by` their own bodies already documented. Inbox is now empty.
- **Labels / ranks**: 9 fixes — missing bug/feature axis added (`#XZZB`, `#SP4N`, `#C6YX`, `#H7KP`, `#9V1F` → feature; `#N3WD`, `#D5MC`, `#265N` → feature/bug as fits); `#K9SR` (→ bug) **refused by the tool** three times ("changed since you read it" with a matching hash, fresh read before each write), so it still reads `change` only. No rank changes; ranks looked ordered.
- **Sections**: unchanged.
- **Left alone**:
  - **needs-qa-llm (105)** — correctly parked; closing needs a non-implementer's verdict. This checkout is a snapshot (one "board snapshot" commit, no `docs/qa_evidence/` tree), so evidence folders and landed commits could not be verified on disk for any card; statuses were judged from each card's body, thread and front matter.
  - `#W5N2` in-progress is real active work (notifications/multiplayer still unbuilt); `#8E4Q` and `#KKYC` stay discussing on open owner questions — `#KKYC` got a note that `#0C7V` + `#D60R` already landed both click paths, so the owner may just close it.
  - Deliberately not merged: `#NK66`/`#4WHD` (later, different UI layer), `#QRNJ`/`#308N` (feature vs. its silent-failure fix), `#K13B`/`#P4GP` (site copy vs. beta release), `#AGN8`/`#1T0W`/`#9V1F` (three landed queue pieces), `#XZZB`/`#SP4N` (analysis vs. implementation, already cross-linked).
  - **`#F36V` filed**: the output-token-limit card's id is literally `OT32` — the letter O is outside the board's base32 alphabet, so no board tool can read, move or relabel it; its file was not touched.
