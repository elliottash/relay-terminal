# #48S3 — the card page's Execute, Plan and Verify buttons name a working pane and reveal it

Commit: `2bfe71aee0659e5660a924c608ac308639394894` (src/BoardPane.cpp, tests/boardexecute_test.cpp).

## What changed

- `CardDetail::show()` keeps the card's `session` token and whether that pane is still open.
- `CardDetail::paneInStatus()` and its three predicates — `paneExecuting()` (status
  `executing`/`in-progress`), `panePlanning()` (`planning`) and `paneVerifying()` (a `needs-qa-*`
  lane): a live token plus a status that still says the pane is working on it.
- `setModeTips()`: in those states the button reads `Executing (abcdef12)` / `Planning (abcdef12)`
  / `Verifying (abcdef12)` (first 8 chars of the session token, the same surface form as the
  claim chip and the thread's hand-off entries), and its tooltip says the click reveals the pane.
  In every other state the buttons are `Execute (x)` / `Plan (p)` / `Verify (v)` again.
- The click handlers — and the `p`/`x`/`v` keys, through the same guards in `plan()`,
  `execute()` and `verify()` — call `onFocusPane(token)` in those states: the same handler the
  claim chip (#R9G7) and the thread's `relay-pane:` links (#HKAP) use — instead of starting a
  second turn, and no key hint fires.

## Verification

- `ctest -R boardexecute` (tests/boardexecute_test.cpp): each of the three buttons reads
  `…ing (abcdef12)` while a live pane holds the card in its status and the click lands on
  `onFocusPane` with the full token; a closed pane's claim (and, for Execute, a landed card's
  live claim) reads `Execute (x)` / `Plan (p)` / `Verify (v)` again.
- `ctest -R boardpane` passes (card-page regression suite).
- `scripts/relay-build` green; land.py's exact-tree build gate passed for both commits
  (`2bfe71aee065`, `70c5be225d93`).

## Notes for the verifier

- Live check: claim a card from a pane, keep the pane open, open the card on the board — the
  action row should read `Executing (········)` and clicking it should raise that pane, not open
  a new one. Close the pane: the button returns to `Execute (x)`.
- The label sheds its key hint via the usual fitButtons() path; the executing label has no key.
