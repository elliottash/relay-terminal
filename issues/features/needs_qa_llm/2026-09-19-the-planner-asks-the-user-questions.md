---
id: MQ9C
type: work
status: needs-qa-llm
labels: [feature, ui]
component: [agent, worker, gui]
milestone: beta
workstream: agent
rank: '3e'
created: '2026-09-19'
acceptance: in plan mode the agent can ask a numbered multiple-choice question before it writes the plan; the pane shows it in the amber "needs human" ink, the pane and its tab go to the needs-you state, and the answer goes back to the waiting turn
source: 'issues/feature_intake.txt, 2026-09-19: "aksing questions. the planner doesnt do it yet."; owner in session, same day: "they should be presented to the user with the amber-orange \"needs human\" color theme"'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-19-the-planner-asks-questions/'], related: [4E13, C1HH, Z0VG], github: null}
---
# The planner asks the user questions, in the amber "needs human" ink

## Issue

aksing questions. the planner doesnt do it yet.

they should be presented to the user with the amber-orange "needs human" color theme

## What plan mode does today

`backend/relay_core/planning.py` gives the planner one instruction — investigate, then call
`write_plan` once — and no way to reach the user in between. The only question it can ask is a
sentence at the end of its reply, which `relay::panestatus::endsWithQuestion` notices well enough to
put the pane in `NeedsYou` (amber, `t.warning`) after the turn ends. So a planner that is unsure
either guesses and writes the plan, or stops and hopes the user reads the last line. Cursor, Warp,
opencode and Claude Code all ask *during* planning, before the plan exists.

## Prior art (read out of the shipped binaries, 2026-09-19)

**opencode** (`opencode-ai` npm binary, bundled source). A `question` tool, allowed for the `build`
and `plan` agents and denied for every subagent (`question: "deny"` in the base ruleset). Input is a
list of questions, each `{question, header (max 30 chars), options: [{label (1-5 words), description}],
multiple?, custom? (default true)}`; answers come back as arrays of labels. The description says, in
full:

> Use this tool when you need to ask the user questions during execution. This allows you to:
> 1. Gather user preferences or requirements
> 2. Clarify ambiguous instructions
> 3. Get decisions on implementation choices as you work
> 4. Offer choices to the user about what direction to take.
> - When `custom` is enabled (default), a "Type your own answer" option is added automatically; don't include "Other" or catch-all options
> - Answers are returned as arrays of labels; set `multiple: true` to allow selecting more than one
> - If you recommend a specific option, make that the first option in the list and add "(Recommended)" at the end of the label

The tool result is fed back as `User has answered your questions: "<q>"="<a>". You can now continue
with the user's answers in mind.`, with `Unanswered` for a question the user skipped. The TUI draws
numbered options with the description under each, `[✓]` boxes when `multiple`, a final "Type your own
answer" row that opens a textarea, and a Confirm button; the selected row is drawn in the theme's
`secondary`, a chosen one in `success`. No amber.

Its **experimental** plan prompt is the one that tells the model to ask, and it is worth copying
almost word for word:

> Ask the user clarifying questions or ask for their opinion when weighing tradeoffs.
> **NOTE:** At any point in time through this workflow you should feel free to ask the user questions
> or clarifications. Don't make large assumptions about user intent. The goal is to present a well
> researched plan to the user, and tie any loose ends before implementation begins.

with, in its phases: "After exploring the code, use the question tool to clarify ambiguities in the
user request up front"; "Use question tool to clarify any remaining questions with the user"; and the
rule that ends the turn — "your turn should only end with either asking the user a question or calling
plan_exit", plus "Do NOT use question tool to ask 'Is this plan okay?' - that's what plan_exit does."
The *shipped* (non-experimental) plan prompt says none of this; it only forbids edits. So opencode's
own answer to "the planner doesn't do it yet" was to write the asking into the plan prompt.

**Warp** (`/opt/warpdotdev/warp-terminal/warp`). Asking is a first-class agent action,
`warp.multi_agent.v1.AskUserQuestion`: `questions: [{question_id, question, multiple_choice:
{options: [{label}], recommended_option_index, is_multiselect, supports_other}}]`, answered by
`AnswerItem {question_id, multiple_choice: {selected_options, other_text}}` or an empty answer for a
skip. Two things Relay should take: the **recommended option is part of the wire format**, not a
convention in the label; and asking is a **permission**, one of the execution-profile settings, with
three states — "The agent may not ask the user questions", "Questions are suppressed only during
auto-approval", "Questions are always available to the agent". The prompt that tells the agent when
to ask is server-side and not in the binary. The block collapses after answering to `Q: … A: …`, and
the conversation summarises as "Answered all N questions".

**Claude Code** `AskUserQuestion` (per `docs/SCRATCHPAD-DESIGN.md`) matches opencode's shape: multiple
choice or multi-select, a recommendation, an explanation under each option, keyboard navigation.

## The shape Relay takes

- Tool `ask_user`, opencode's schema plus Warp's explicit recommendation, offered in both modes
  (owner, 2026-09-19: "let the non-plan agent use the questions as well (like warp / claude)") and
  never to a subagent (it cannot see the pane, #C1HH's rule).
- **`options` is optional** (owner, same day: "dont force multiple choice -- allow open-ended
  questions"). With options a number is the whole answer; without them the question is open and the
  prompt box takes the words. `/skip` passes on either.
- A blocking round trip on the pattern of `type_into_program` (`program_input.py`): the worker emits
  `question`, the pane draws it, the pane answers `question_answer`, the turn thread wakes. Unlike
  that one it has no reply deadline — the user may be away — only Stop and a new prompt end it.
- Presentation: the amber `warning` ink, as #4E13 asked for ("questions or items needing human
  response could be in bold amber"), and the pane state is `NeedsYou` while one is open, so a
  background tab says so too.

## Tasks

- [x] `backend/relay_core/questions.py`: the tool, its validation and the blocking round trip <!-- t:ta -->
- [x] plan-mode prompt tells the planner to ask before it writes the plan <!-- t:xc -->
- [x] worker and protocol: `question` / `question_answer`, documented in `docs/AGENT-SESSIONS-PROTOCOL.md` section 27 <!-- t:qb -->
- [x] the pane draws the card in amber and answers it; keyboard first (numbers, `0` to skip, own words) <!-- t:0x -->
- [x] pane state goes `NeedsYou` while a question is open, so a background tab says so <!-- t:r0 -->
- [x] the turn clock says "waiting for your answer" rather than "thinking" while the card is up <!-- t:yr -->
- [x] `question` / `question_closed` classified as forwarded in `remote/wire.py`, and a line from a <!-- t:7p -->
      paired phone answers the card
- [x] a shortcut hint on the slow path: typing an option out in full → "Next time: just type 2" <!-- t:h2 -->
- [x] tests: `tests/test_questions.py` (19), `tests/panestatus_test.cpp`, `tests/test_tool_labels.py` <!-- t:jy -->
- [x] implementer evidence: `docs/qa_evidence/2026-09-19-the-planner-asks-questions/` <!-- t:dm -->

## Implementation

Assignee: agent (Claude Opus 5). `backend/relay_core/questions.py` holds the tool and the round
trip; `backend/relay_core/tools.py` offers it (`can_ask`, off in `subagents.RestrictedExecutor`);
`backend/worker.py` takes `question_answer`; `backend/relay_core/planning.py` tells the planner to
ask; `src/Pane.h` has `Ink::Ask` (the amber, bold) and the `Ask` card; `src/PaneStatus.{h,cpp}`
gained `Facts::questionOpen`.

## Deliberately left

- **A button row.** The card is text in the terminal and the keyboard answers it, because that is
  what Relay is; opencode's TUI does the same. Whether it should also be clickable, the way the
  Switchboard's cards are, is the owner's call about how far the pane's chrome goes.
- **Whether asking should be a permission.** Warp makes it a per-profile setting with three states
  ("may not ask" / "suppressed during auto-approval" / "always available"). Relay offers the tool
  unconditionally in both modes, which is opencode's arrangement. If the owner ever wants a pane
  that must not interrupt — an unattended run, a scheduled turn — that setting is where it goes.
- **Auto-compaction announces rounds that do nothing.** While a conversation is over the limit,
  compaction runs once per turn, and a round with nothing left it can compact still emits
  `compaction_started` / `compacted` with `before == after` and still pays for a side-model call.
  It already did that on main (the first round of
  `test_auto_compaction_summarizes_and_keeps_tool_groups_intact` is one); adding a tool spec to the
  list only changed which round lands last, which is why that test now asserts that *a* round
  reduced rather than that the last one did. Whoever owns compaction should decide whether a no-op
  round should be skipped, or reported as one.
