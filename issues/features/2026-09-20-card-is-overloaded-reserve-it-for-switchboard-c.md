---
id: VQ8T
type: work
status: executing
labels: [feature, switchboard, docs]
assignee: claude-code
rank: m
created: '2026-09-20'
source: 'conversation, 2026-09-20'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# "card" is overloaded: reserve it for Switchboard cards, rename the ask and the help popup

## Issue
"card" is currently overloaded and means issue card and question notificiation card (and maybe
others). examine that and help me decide what to do -- probably reserving card for the switch
board issue cards.

## Decisions

Owner, 2026-09-20, on the survey below: *"i agree with these recommendations."*

A survey found eight distinct senses of the word.

| Sense | Verdict |
|---|---|
| Switchboard record, four types | keep the word |
| question and approval prompt | rename to **ask** |
| help popup over the composer | rename to **popup** |
| turn card on the phone (designed, not built) | rename before it is built |
| plan block in the web client | rename the local variable |
| model card, vendor documentation | keep, always qualified |
| approval cards in competitor research | keep, always attributed |
| `card #ID` as a spec citation | keep the convention, ban the bare form |

1. **`card` means a Switchboard record of any of its four types** — `CARD_TYPES = ("work",
   "plan", "memory", "alias")`, `backend/relay_core/board.py:167`. Say *work card*, *plan card*,
   *memory card* or *alias card* when the type matters. It is **not** narrowed to issue cards:
   that would strand the other three types with no noun.
2. **The question/approval prompt becomes "the ask"** (and "the approval ask" for protocol 27.6).
   It draws no card at all — `Pane::printQuestion` prints inline terminal text in `Ink::Ask` with
   no border or surface. The surrounding code already says ask everywhere: `m_ask`, `Ink::Ask`,
   `ask_user`, `approvals_ask`, and the wire events `question`/`question_closed`/
   `question_answer`. The owner never used the word "card" for it; sessions introduced it in
   comments. Not "prompt", which already means the prompt box; not "question", because approvals
   are not questions.
3. **The help popup** (`QFrame#helpCard`, the `?` cheat-sheet over the composer) becomes
   `helpPopup`, matching `NotificationsPopup` beside it. The word "card" stays in the theme's own
   prose where it means a raised rounded surface: that is the CSS idiom, and the board metaphor at
   `src/Theme.cpp:651` is deliberate.
4. **"Turn cards"** in `docs/REMOTE-AND-MULTIPLAYER-DESIGN.md` (the phone thread view, designed but
   not built) are renamed now, while it costs three doc lines. They become **turns**, since the
   protocol already keys those events on `turn_id`. That doc currently has both senses in adjacent
   table rows.
5. **Model cards, competitor research and spec citations keep the word.** A model card is a term of
   art; the research docs describe what Claude Code and Warp really do draw. But a spec citation is
   always written `card #ID` in full and **never** shortened to "the card" in later sentences —
   `app/meet.js` and `remote/cpace.py` currently say "the card" where no board exists anywhere near.
6. **The rule is recorded in `WARP.md`**, which every session reads first and which today contains
   the word "card" zero times. There is no glossary anywhere in the repo, and this is a naming rule
   sessions keep breaking.

Deliberately **not** in scope: the terminal engine's `Link` struct carries fields named `card` and
`cardTitle` (`engine/TerminalBackend.h:244-249`), putting board vocabulary in a layer that knows
nothing about the board. That is a layering question, not a naming one, and folding it in would
widen the change for no gain.

## Plan

Six identifier renames, six test assertions that pin them, three user-visible strings, three doc
lines, and roughly 300 comment and doc lines to reword.

- `relay::input::cardTakesRemoteLine` → `askTakesRemoteLine` (`src/InputPolicy.h:106`,
  `src/InputPolicy.cpp:131`, two call sites in `src/Pane.h`)
- `RemoteLine::cardOpen` → `askOpen` (`src/InputPolicy.h:98`)
- `Pane::unreadableCard` → `unreadableAsk` (`src/Pane.h:2705,2764`)
- `m_helpCard` / `toggleHelpCard` / `QFrame#helpCard` → `m_helpPopup` / `toggleHelpPopup` /
  `QFrame#helpPopup` (`src/Pane.h:8509-8567`, `src/Theme.cpp:98,148,177,480`)
- `ApprovalCardTests` / `PaneApprovalCardTests` → `ApprovalAskTests` / `PaneApprovalAskTests`
  (`tests/test_approvals.py:200,536`)
- the `card` local holding a plan block → `plan` (`app/app.js:1037-1048`)

User-visible strings:

- `src/Pane.h:2954` `"Answer 1–4 · this card takes a number or the word itself"`
- `src/ApprovalsPane.h:91` `"No approval cards; what Relay has always done"`
- `src/RelayWindow.h:2188` `"Ask me puts each one to you as a card: …"`

The reword lands hardest in `src/Pane.h` (~98 lines), which holds the ask at 2629-2990 and the
Switchboard chip and index a few hundred lines below. That file takes its own claim window rather
than sharing one with the docs.

## Tasks
- [ ] identifier and test-class renames, plus the three user-visible strings <!-- t:x1 -->
- [ ] reword the ask-sense comments in `src/` and `backend/` <!-- t:x2 -->
- [ ] reword the ask-sense prose in `docs/`, and retire "turn cards" <!-- t:x3 -->
- [ ] the naming rule in `WARP.md` <!-- t:x4 -->
