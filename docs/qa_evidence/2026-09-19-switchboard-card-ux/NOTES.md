# The card reads as one page (#VZ69, 2026-09-19)

Owner's six asks on the Switchboard's card detail and its quick add, in `issues/features/…`
(#VZ69). Design: `docs/SWITCHBOARD-DESIGN.md` 4.12. Implementer evidence — not a QA verdict.

## How this was run

`qa-setup.sh` in this folder: a throwaway git repo under `/tmp/vz69qa/proj` with a two-card
`issues/` board, its own `XDG_CONFIG_HOME` / `XDG_DATA_HOME` / `XDG_CACHE_HOME` /
`XDG_RUNTIME_DIR` / `TMPDIR`, `RELAY_KEYRING=off` (so the owner's remote identity key is never
touched) and `provider/preset=glm-coding` in that config. This repository's own `issues/` was
never opened. Private **Xvfb `:87`** at 1500x950, driven with `xdotool`, captured with `import`.

The binary is **not** the shared `build/`: that tree holds several sessions' uncommitted work.
It is `main` plus this work only — `git archive <tip> | tar -x` into `/tmp/vz69/src`, the four
files of this change three-way merged onto it, then `cmake --build … --target relay`
(`RELAY_QA_BUILD=/tmp/vz69/build`). So what was photographed is what lands.

The glm-coding key came from the desktop keyring through `relay_core.keystore.lookup` in a
separate process and was passed to Relay as `RELAY_GLM_CODING_API_KEY`; it is in no file, script
or shot here, and the copy used was shredded after the run. Model: `glm-5.3`.
Paid calls: one Discuss (completed) and one Plan (cancelled after ~3s by the button under test).

## The shots

| File | Shows |
|---|---|
| `implementer-02-quickadd-field` | The quick-add field, open on `n`: "Title of a new card in Ready to start — Enter opens it, Esc closes". |
| `implementer-03-new-card-opens-editing` | Enter created `#EYXV` and the card **opened editing**, cursor in the issue box, the field closed behind it. The line typed is the title (in the title field) and is offered **selected** in the issue box, so the next keystroke replaces it. The pencil is on the title, greyed while its own editor is up. |
| `implementer-04-card-read-view` | The card after Ctrl+Enter: `✎ Edit (e)` outlined in the accent at the right of the title; `Issue` then the **THREAD · 3** band with a rule above it; the reply row is **Plan (p)** and **Execute (x)** only. |
| `implementer-05-agent-discussing-strip` | Enter in the box discussed with no button: `✦ Agent is discussing…` with `✕ Stop discussing` over the box, Plan and Execute disabled, the thread's own "✦ agent Discuss / thinking…" unchanged. |
| `implementer-06-after-discuss` | The turn done: the strip is gone and the buttons are back. |
| `implementer-07-agent-planning-strip` | Plan (p) clicked: the strip names *this* turn — `✦ Agent is planning…` / `✕ Stop planning` — and the slow path taught its key ("Next time: p"). |
| `implementer-08-stop-planning-clicked` | `✕ Stop planning` clicked: `cancel` went, the strip closed, the buttons came back, and the thread kept the owner's "Plan this card." with no agent reply. |
| `implementer-09-issue-thread-seam` | A plain card: the seam between the issue and the thread on an empty thread, whose line now teaches Enter / Ctrl+Enter / Ctrl+Shift+Enter instead of naming buttons. |
| `implementer-10-comment-without-a-button` | Ctrl+Shift+Enter left a `note` on the thread with no model call and no Comment button. |

## Unit tests

`ctest --test-dir build -R board` — `tests/boardmodel_test.cpp`:
`quickAddNamesTheSectionItAddsTo` (the field's wording, the write, the card opening on its issue
box with the seeded line selected), `theBoxDiscussesAndTheRowPlansOrLeavesTheBoard` (no Discuss or
Comment button; the strip's two labels, its ✕ and the disabled buttons; `cancel`),
`theTitleAndTheIssueAreEditedOnTheCardAndSavedThroughTheWorker` (the pencil's label, that it opens
the editor and goes quiet while it is up), and the seam's rule and band in the document.
