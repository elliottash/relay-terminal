---
id: VZ69
type: work
status: needs-qa-llm
labels: [feature, switchboard]
assignee: agent
implemented_by: claude-opus-5
rank: zzzzzx
created: '2026-09-19'
links: {plans: [], commits: [e079f96], evidence: [docs/qa_evidence/2026-09-19-switchboard-card-ux/], related: [QG60, XS6Q], github: null}
---
# improving the switchboard functionality

## Issue
when you first press enter to add a new card, it should open the edit box, the editable issue part.

the first thing you enter in teh top row thing makes the title, not the issue content. 

there should be a promponent pencil edit button, rahter than the small "edit" button at the top. 

there should be a clearer dematcation between the issue and the convo thread.. 

remove comment / discuss buttons. i would say you just press enter in the prompt box to discuss / comment.

"stop" button isnt intuitive, it should be stop planning i guess, or there should be an X next to "agent planning".

## Resolution
All six asks are in, in `src/BoardPane.cpp` (with four stylesheet rules in `src/Theme.cpp`).
Design: `docs/SWITCHBOARD-DESIGN.md` 4.12, which also amends 4.9. Evidence:
`docs/qa_evidence/2026-09-19-switchboard-card-ux/`.

1. **Enter on a new card opens the issue editor.** `board_written` for a `board_create` now closes
   the quick-add field, opens the new card and starts editing it with the cursor in the issue box
   (the same path `e` takes, so it waits for the card to arrive the same way).
2. **The top row makes the title.** The field says so — "Title of a new card in Ready to start —
   Enter opens it, Esc closes" — and the line is the card's `# ` heading. The worker still seeds
   `## Issue` with it, because that is the owner's words and the card format keeps them verbatim,
   so the editor offers that line **selected**: the first keystroke replaces it, and Esc or an
   empty save leaves the card exactly as the field made it rather than blanking the only words it
   has. (Making the file's `## Issue` start empty instead would be a `board_create_card` change —
   the tool's rule is that a card records the user's words verbatim, and its files were being
   edited by another session. Say if you want the section genuinely empty on disk.)
3. **A prominent pencil.** `✎ Edit (e)` left the row of muted text buttons at the top and sits at
   the right of the title, outlined in the accent (`QToolButton#boardEditPencil`) — the one control
   on the card styled as obviously pressable. Click-the-title, double-click-the-text and `e` are
   unchanged.
4. **A seam before the thread.** `THREAD · n` now sits on a ground of its own the width of the
   document with a hairline rule above it, instead of being a heading in bolder ink.
5. **No Comment or Discuss buttons.** Enter discusses, Ctrl+Shift+Enter comments, and the
   placeholder and the empty-thread line say so. The row keeps Plan (p), Execute (x) and, in a QA
   lane, Verify (v) — what is *not* typing into the box.
6. **Stop says what it stops.** While a turn runs, a strip over the reply box reads
   `✦ Agent is planning…` / `✦ Agent is discussing…` with `✕ Stop planning` / `✕ Stop discussing`
   at its right. No button changes its label any more. This is what makes 5 possible: the old Stop
   lived on the Discuss button, which no longer exists.

Also fixed on the way through: a queued `e`, `p`/`x`/`v` or reply-focus for a card that never
opened (because another card was being edited) was left pending and fired at whatever card opened
next; it is now dropped with the card that did not open.

## QA checklist
- [x] `ctest --test-dir build -R '^board$'` — 55 pass, including the three tests named in NOTES.md.
- [ ] `ctest --test-dir build` fully green and `./scripts/test.sh` — not run here: the shared
      checkout had several sessions' half-finished work in `src/Pane.h` and `src/Projects.cpp`,
      so the full tree did not build. The binary under test was `main` plus this work only.
- [ ] On a live board, `n` → type a title → Enter opens the new card with the cursor in the issue
      box, the typed line selected, and the quick-add field closed.
- [ ] Typing over the selection and Ctrl+Enter writes only the new issue; Esc instead leaves the
      card with the typed line as its `## Issue`.
- [ ] The pencil is at the right of the title, opens the editor, and is disabled while the editor
      is up. `e`, a click on the title and a double-click in the text still open it.
- [ ] The thread's band and the rule above it are visible on every shipped theme, light and dark
      (checked here on the default dark theme only).
- [ ] The reply row has no Comment or Discuss button; Enter discusses, Ctrl+Shift+Enter leaves a
      note with no model call.
- [ ] While a turn runs the strip names the mode and its ✕ stops it; the other buttons are
      disabled and keep their labels. Both `discuss` and `plan`.
- [ ] At ~350 px pane width the reply row still fits or wraps cleanly (it lost two buttons, so it
      should be easier than before).
