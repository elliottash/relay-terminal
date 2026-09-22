---
id: CPY4
type: work
status: inbox
labels: [bug, remote]
assignee: null
rank: zcpy4
created: '2026-09-22'
source: 'Measured by Claude Code driving the phone app at 390x844, 2026-09-22'
links: {plans: [], commits: [7f945e42], evidence: ['docs/qa_evidence/2026-09-22-phone-ux-drive/'], related: [PH0N], github: null}
---
# Copy id on the phone says nothing, copies nothing on iOS, and the sheet jumps every minute

## Issue
Four faults around the Conversations sheet, reproduced at 390×844
(`docs/qa_evidence/2026-09-22-phone-ux-drive/`, findings 4–6).

**1. Every outcome is painted under the sheet.** The toast lives in `.rp-term-wrap`, which has no
z-index (`app/pane.js:227`, `:350`); the sheet layer is `z-index: 10` over `inset: 0` with a
70 %-opaque backdrop (`app/pane.css:477`). Copy id is the one sheet control that does **not** call
`closeSheet()` first, so the sheet is guaranteed to still be up when the answer lands. Measured
with `elementFromPoint` at the toast's centre: the topmost element is `rp-session-when`, a row of
the sheet. "Conversation id … copied", "Clipboard refused — long-press to copy" and "The id was
refused: …" are all equally invisible, so the button is indistinguishable from dead.
`tests/test_pane_view.py:490` reads the toast's `textContent`, which passes on an invisible toast.

**2. The clipboard write is outside the user gesture.** The tap only sends `conversation_id`
(`app/pane.js:655-661`); `navigator.clipboard.writeText()` runs later in the answer handler
(`:1303-1320`), after a wire round trip. This codebase already records the rule at
`app/app.js:802` — "First thing in the gesture: Safari drops the user activation across an await."
In this drive's headless Chrome the write **was** refused and the fallback ran, which is the iOS
path. The shape that works is the reverse: fetch the id when the sheet opens (or on the previous
gesture) and let the tap do a synchronous write.

**3. The fallback lands nowhere.** It appends the id to `document.querySelector('.rp-sheet')` — a
document-global, kind-blind query against an element `closeSheet()` leaves in the DOM (it only
hides the layer and clears the text). With the sheet dismissed the id is appended to a hidden
layer; with the row-actions or send-when sheet open it is appended to that one, under
"Send now / Queue / Steer".

**4. The sheet throws the reader back to the top every minute.** The rebuild is gated on a
signature of the whole `sessions` block (`app/pane.js:1093-1097`), which includes each row's
`when` — a relative time the desktop recomputes on every publish and which steps at one-minute
granularity. Measured on the 50-row fixture: scrolled to `scrollTop: 2010`, one `when` tick →
`scrollTop: 0`, focus back on "New conversation", and fault 3's fallback line deleted with it.
While a turn runs `pane_state` arrives about ten times a second, which is what the guard above it
was added for; the guard just does not exclude the clock.

Also on this path: `conversation_id` is not in the outbox's `QUEUEABLE` and the view's `send` passes
no `onError` (`app/app.js:926`), so tapping Copy id while the link is down is swallowed silently
and leaves `idRequest` set, waiting for an answer that was never asked for.
