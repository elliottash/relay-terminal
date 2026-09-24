---
id: X7NB
type: work
status: needs-verification
labels: [feature, switchboard]
assignee: agent
rank: m
created: '2026-09-20'
source: 'conversation, 2026-09-20'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-20-drop-plan-cards/'], related: [VQ8T, W3KD], github: null}
---
# Drop the plan card type: the protocol already says plans live on the work card

## Issue
Owner, 2026-09-20, after an audit of the non-work card types: **"drop plan cards."**

`CARD_TYPES = ("work", "plan", "memory", "alias")` (`backend/relay_core/board.py:167`) declares a
plan card type that has format, storage and folder support and **no runtime behaviour at all**:

- The protocol already says it is not used. `docs/AGENT-SESSIONS-PROTOCOL.md:2533`: *"The plan is
  the card's own `## Plan` section — design 12.4, 'plan mode writes the plan onto a card', rather
  than a separate `type: plan` card"*.
- Plan mode writes plain Markdown to `<root>/.relay/plans`, not cards (`planning.py:59-80`,
  `agent.py:540`).
- The board's Plan stage writes a `## Plan` section on the **work** card, and `stage_advance`
  returns `None` for any card whose type is not `work` (`board_tools.py:2219`).
- A user cannot create one: quick-add hard-codes `card_type: "work"` (`src/BoardPane.cpp:3761`).
- `board_list` hides every non-work card by default (`board_tools.py:1682-1683`).

**Nothing is orphaned.** There are zero `type: plan` cards on this board. The one card in
`issues/planning/` is `type: work` — `planning` is also a *tab* name, which is part of why the type
was confusing: `PLAN_FOLDER = "planning"` collides with a category folder that work cards use.

## Decisions
- Drop `plan` from `CARD_TYPES` and remove the type's format support.
- **Keep** everything else that is called "plan" and is unrelated: the `## Plan` section on a work
  card, the Plan turn mode (`CARD_MODES = ("discuss", "plan")`), the `plan` thread-entry kind, the
  `planning/` **tab**, and plan mode's own `.relay/plans` files.
- Fix `docs/AGENT-SESSIONS-PROTOCOL.md:2533`, which additionally claims a card whose `links.plans`
  names plan cards "has them read as context". No code does that — only `forge_sync._plan_links`
  reads the field, and only to print ids into a GitHub issue body.

## Plan
1. `backend/relay_core/board.py`: drop `plan` from `CARD_TYPES` (:167), `PLAN_STATUS_FOLDER`
   (:179-186) and its entry in the status-folder table (:187), `PLAN_FOLDER` (:194), `PLAN_FIELDS`
   (:216) and its allowed-field set (:227), the `TASK_HEADING` entry (:240), the `expected_folder`
   branch (:821-822), the `card_paths` extra folder (:1078), `:1524`, and the `_status_from_folder`
   branch (:1818-1819). Leave the `plan` **thread-entry kind** at :838 alone.
2. `backend/relay_core/board_tools.py`: the `B.PLAN_FOLDER` branches at :1830, :2038 and :2482.
   Note #W3KD wants an `alias` branch added at the same two sites — coordinate or land after it.
3. `src/BoardModel.cpp:1015`: stop tagging a `planning` tab as `type="plan"`. Check what
   `BoardPane.cpp:4391-4394` (`defaultCategory`) then does, since excluding the planning tab from
   the default is the only thing that tag is used for and that behaviour should not change.
4. `backend/relay_core/forge_sync.py`: `_plan_links` (:1695-1705) and its three call sites
   (:1396, :1410, :1441) now always return empty. Remove them and the "Relay plan cards (not
   synced)" footer line (:518-519).
5. Docs: `docs/AGENT-SESSIONS-PROTOCOL.md:2533`, `docs/GITHUB-SYNC.md:17,52`,
   `backend/relay_core/board.py:2111` (the generated tab bullet says "plan cards are in
   `planning/`"), `backend/relay_core/aliases.py:458`.
6. Tests: `tests/test_board.py:414-429`, `tests/test_board_tools.py:255-262`,
   `tests/boardmodel_test.cpp:344-360`, and `tests/test_forge_sync.py` wherever it covers plan
   links. A new test should assert `"plan" not in CARD_TYPES` and that a card file declaring
   `type: plan` is refused with a clear message.

## Deliberately left
`links.plans` stays in the front-matter schema. 331 cards carry `plans: []`, so removing the key is
a format migration across the whole board — `relay-board.py migrate` territory, and a poor fit for a
checkout with twenty live sessions holding card files. After this card the key is inert: nothing can
populate it and nothing reads it. Retiring it is its own card and its own decision.

## Tasks
- [x] `board.py`: the type, its folders, fields and heading <!-- t:p1 -->
- [x] `board_tools.py`: the three `PLAN_FOLDER` branches <!-- t:p2 -->
- [x] `BoardModel.cpp`: the planning-tab type tag, without changing `defaultCategory` <!-- t:p3 -->
- [x] `forge_sync.py`: `_plan_links` and the footer line <!-- t:p4 -->
- [x] docs, including the false `links.plans` context claim <!-- t:p5 -->
- [x] tests, including one that refuses `type: plan` <!-- t:p6 -->

## Implementer check
Landed 2026-09-20. `plan` is gone from `CARD_TYPES`, with `PLAN_STATUS_FOLDER`, `PLAN_FOLDER`,
`PLAN_FIELDS`, the `TASK_HEADING` entry, the `expected_folder` / `tab_of` / `_status_from_folder` /
`card_paths` branches, the three `B.PLAN_FOLDER` branches in `board_tools.py`, the `type="plan"`
tag on the `planning` tab in `BoardModel.cpp`, `forge_sync._plan_links` with its three call sites
and the "Relay plan cards (not synced)" footer, and the doc lines.

Two notes on the plan as written:

- **`defaultCategory`.** The `type="plan"` tag was only ever read at `BoardPane.cpp:4391` to keep
  the `planning` tab out of quick add's default. With the tag gone the tab is `type="work"`, so the
  skip is now written there by name, with the history in a comment. Same answer as before on every
  board (`features` is first in the default config); unchanged on a board that lists `planning`
  first, which the tag was what protected.
- **#W3KD fell out of the same two lines** and is fixed here: `board_move_card` and `board_split_card`
  now send an `alias` card to `aliases/`, as they do a memory card, instead of the strict tab
  lookup that made `unknown tab 'aliases'` and left an alias card impossible to retire. Covered by
  `tests/test_board_tools.py:MoveTests.test_an_alias_card_is_retired_and_brought_back`.

Out of scope, as the card says: `links.plans` stays in the schema (331 cards carry `plans: []`);
it is now inert — nothing writes it and nothing reads it.

## QA checklist
- [ ] `"plan" not in board.CARD_TYPES`, and `PLAN_FOLDER` / `PLAN_FIELDS` / `PLAN_STATUS_FOLDER` are gone — `tests/test_board.py:CardTypeTests.test_there_is_no_plan_card_type`.
- [ ] A card file declaring `type: plan` is refused with `unknown_type: type 'plan' is not one of ('work', 'memory', 'alias')`, and `board_create_card {type: "plan"}` answers "type must be one of work, memory, alias." — same test and `tests/test_board_tools.py:CreateTests.test_there_is_no_plan_card_type`.
- [ ] Still there and still working: the `## Plan` section and `stage_advance`'s `plan-written`, the Plan turn mode (`CARD_MODES`, `CARD_MODE_BOARD_TOOLS["plan"]`, `PLAN_HEADING`), the `plan` thread-entry kind, the `planning/` tab holding `type: work` cards, and `<root>/.relay/plans`.
- [ ] Quick add still files into `features`, not `planning` (`BoardView::defaultCategory`).
- [ ] An alias card round-trips active → retired → active (#W3KD).
- [ ] An issue body's footer carries only the `relay-id` marker; no "Relay plan cards (not synced)" line.
- [ ] Regression: `tests.test_board tests.test_board_tools tests.test_forge_sync` (426) green, `ctest -R board` (4) green, `relay-board.py check` 0 errors. `tests.test_board_protocol` has 2 failures that also fail on a clean `git archive HEAD` export (another session's area, unrelated).
