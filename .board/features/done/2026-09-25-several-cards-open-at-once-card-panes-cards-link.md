---
id: C7RW
type: work
status: dropped
labels: [feature, switchboard, panes, design]
waiting_on: owner
duplicate_of: P2W8
rank: zzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
source: Owner in a Relay pane, 2026-09-25 (two messages, the second mid-turn)
links: {plans: [], commits: [], evidence: [], related: [Y2BA, AGNT, CTRN, E85D, P2W8, C7PF, DR4K], github: null}
---
# Several cards open at once: card panes, cards linked to panes, or both

## Issue
feature to discuss -- replace the swithcboard split view (list and cards) with card panes. it woudl often be convenient to have multiple card threads open. a design risk to discuss, if that just becomes the same as a regular pane. and we should instead have cards attachable to panes and more of the card features integrated into standard panes. explore and discuss all options

one a pproach -- a card can be a linked artifact with a pane, similar to the feature where we have a liked canvas document.

## Discussion points
**Where the limit is today.** One Board pane per tab (`RelayWindow::openBoardCard` finds "the one the tab already has"), one `CardDetail` per `BoardView`, so one open card page per tab. The backend already runs one conversation and one `TurnSupervisor` per open card (#DR4K, #CTRN: key `<tab>/card:<ID>`), so several card threads at once is a GUI limit only. #Y2BA (inbox) asks for the same thing ("pressing new card multiple times splits").

**What already makes a terminal pane card-aware.** The header chip (#C7PF/#0FBB) names the turn's card and every claimed card; `#` picker attaches cards to a prompt; Run/Verify hand a card to a pane (`startBoardTask`); `board_claim` writes the pane token into the card. The chip's click only opens the Board today.

**Options.**

- **A. Card panes** — lift `CardDetail` + its `CardContext` console into `ToolPane::Kind::Card`, one leaf per open card; Enter reuses the tab's card pane, Shift+Enter (or a pop-out button) opens another. Board pane becomes list (+ optional preview). Needs a layout node `{"card": {workspace, id}}`, a per-tab shared `BoardModel`/watcher, and `openBoardCard` routing to an existing card pane. Cost: `CardDetail` is entangled with `BoardView` (friend classes, selection-follow timer, keys).
- **A0. Cheapest form of A** — allow N Board panes per tab, a second one opened `openCardSolo` with the list permanently hidden and Esc closing the pane. Same worker, duplicate model per instance. Ships in days; A can replace it later without changing the user-facing behaviour.
- **B. Cards attach to terminal panes; card features move into the pane** — a pane attached to #ID shows a card drawer (body, `## Plan`, `## Tasks`, stage) and gains Plan/Verify/Done in its action row. Risk: the pane's conversation is not the card's. #CTRN decided the thread is the settled record written by the worker with provenance; piping a terminal pane's turns into it would make the thread a transcript. So B cannot replace the card page; it can only add a view of the card to the pane that is working it.
- **C. Card as a linked artifact of a pane** (owner's second message) — the #E85D/#P2W8 workspace-group model: a card pane and the terminal pane running it form a group with roles (`card`, `runner`), restore together, navigate to each other. This is the relationship Run already creates implicitly and then forgets. It composes with A rather than replacing it: the card pane is the artifact view, the terminal pane is the console.
- **D. Tabs of open cards inside the one Board pane** — no side-by-side, so it does not meet the want. Rejected.
- **E. Open the card file in the editor pane** — `o` already does this; loses stage, actions and the console. Partial at best.

**The design risk, answered.** Since #AGNT a card page's console *is* a `Pane` (no shell, `CardContext`); a card pane is a console by construction, which is the intended architecture, not drift. What must stay different, and be legible in the chrome: (1) no pty, ever — Run still opens a terminal pane beside it; (2) role `switchboard`, console tool scope, Plan writes only `## Plan`; (3) the record is the thread with `model=`/`turn=` provenance, not the pane's conversation; (4) direction of ownership — a card pane is *about* the card, a terminal pane *claims* it (token in the card). If those four hold, a card pane and a terminal pane share the composer and differ in what they are about, which is the #AGNT rule.

**Recommendation.** A0 now (unblocks #Y2BA), A as the structural follow-up, C as the link between a card pane and its runner using the #E85D group model, and a B-lite card drawer in the terminal pane (chip toggles the card's body/tasks inline, read-only plus Done). Not B in full.
