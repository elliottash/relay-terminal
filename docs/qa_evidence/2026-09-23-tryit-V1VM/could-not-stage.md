# Try it could not be staged (#V1VM)

The card's person-visible change is the model picker's limits line gaining
`· N usage reset(s)` (and the Sources tab the same). That line is data-driven: it
appears only after a guest harness reports `usage_limits`, which needs a live
Codex login (`~/.codex/auth.json` under the running profile). A disposable Try-it
fixture isolates HOME and cannot complete Codex's OAuth device flow, and Relay's
other limit sources (Z.AI/Kimi polling) need API keys the sandbox does not have —
so the staged picker would show the feature to nobody.

Everything machine-checkable is already automated and was run for the card
(`docs/qa_evidence/2026-09-24-usage-resets/`):

- `QT_QPA_PLATFORM=offscreen ./build/relay-modelcatalog-tests` — includes
  `limitsTextNamesEachWindow` (the `· 1 usage reset` / `· 3 usage resets` / hidden
  at 0 wording) and `theWorkersLimitsObjectIsReadWindowsAndStatus` (the presets-row
  field). This is the same seam #XH4K's evidence used.
- The scanner half is visible live outside the app: the user's Google Sheet rows
  now carry column H "5h reset" (e.g. `2026-09-24 01:59` for claude (ashe@ethz.ch))
  and `usage_resets=1` for both Codex logins, from the 2026-09-24 00:02 run.

What a person would eventually judge in their real session: after their next
Codex turn, the picker row should read e.g. `weekly 64% left, resets 21:20 · 1
usage reset`.
