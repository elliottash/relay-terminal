# Implementer evidence — #T71W, the GUI half (tasks `t:b1`, `t:b2`)

Date: 2026-09-19 · implemented by `anthropic/claude-opus-5` (subagent)

The worker's half (`qa_verifiers.py`, the `qa` block on `board_card_get`) is a separate task and
was not running when these were taken, so the pane was driven from a **JSON fixture of the
contract**: the `qa` object exactly as the card's "Where it shows" section specifies it, handed to
the real `relay::BoardView` through the real `board` / `board_card` events. Nothing about the
widget is stubbed — this is the shipped `CardDetail`, the shipped theme and the shipped layout.

## What is in the shots

| File | What it shows |
|---|---|
| `implementer-gui-01-verify-line.png` | A card in `needs-qa-llm` with the recommendation: the line under the fields — *"Verify with Codex (installed) · then GLM-5.3 · Claude skipped: implemented this card"* — and **Verify** live beside Execute. The detail pane is 45% of 1180 px, the default split. |
| `implementer-gui-02-wide-keeps-the-keys.png` | The same card in a wide pane (1700 px): the reply row has room, so every label keeps its key — `Comment (Ctrl+Shift+Enter)`, `Discuss (Enter)`, `Plan (p)`, `Execute (x)`, `Verify (v)`. |
| `implementer-gui-03-no-verifier.png` | The same card with a `qa` block whose every family is skipped or unavailable: *"No verifier available: Claude skipped: implemented this card · OpenAI: not installed · GLM: no key · Kimi: no key"*, and Verify greyed out. |

Shot 02 against shot 01 is also the evidence for the one thing this card changed outside its own
feature: the reply row now **drops the parenthesised keys from its labels when it does not fit**
(`CardDetail::fitButtons`). Before it, the row painted its labels cut off at both ends in a narrow
card — visible at four buttons already, because `CardDetail`'s own 320 px minimum lets the layout
squeeze a button below its size hint — and a fifth button would have made that worse. The keys stay
in the tooltips and in the pane's key legend, which reads `v verify` in every shot.

## How they were taken

Live, under Xvfb, with the profile, the runtime dir and the temporary dir all isolated under a
short path (the 108-byte socket limit) and no keyring:

```sh
R=/tmp/c1/x; mkdir -p "$R/cfg" "$R/run" "$R/tmp"; chmod 700 "$R/run"
export XDG_CONFIG_HOME="$R/cfg" XDG_RUNTIME_DIR="$R/run" TMPDIR="$R/tmp"
export RELAY_KEYRING=off QT_QPA_PLATFORM=xcb
xvfb-run -a -s "-screen 0 1280x900x24" ./harness <out.png> [wide|none]
```

`harness` is a throwaway main() kept out of the tree (it is not a second entry point to maintain):
it creates a `relay::BoardView`, applies the theme, feeds it the two events above and calls
`QWidget::grab()`. It links the same `librelay-board.a` the pane does, with the compile and link
lines taken from `build/CMakeFiles/relay-board-tests.dir/`.

Relay itself was not launched, because the recommendation cannot reach the pane until the worker
sends it: with today's worker the card carries no `qa` block and the line and button are correctly
absent — which is shot 03's left-hand case, and the `a card without qa` assertions in the Qt test.

## Automated checks

`ctest --test-dir build -R '^board$'` — 49 passed, 0 failed. The four slots added for this card:

- `theVerifyLineNamesTheRecommendedVerifierAndWhatItSkipped` — the line text for a guest runner, a
  preset runner, and the no-verifier case; `verifyRunner` / `verifyLabel` / `familyLabel`.
- `theExecuteTaskAsksForTheImplementedByTrailer` — the Execute brief asks for
  `Implemented-By: <your provider/model>` and says the card's own field is stamped for the agent.
- `theVerifyTaskIsTheQaChecklistAndAsksForTheVerifiedByTrailer` — every clause of the Verify brief.
- `aQaLaneCardOffersVerifyOnTheRecommendedRunner` — the line, the button's label and enabled state,
  the click and the `v` key both calling `onVerifyCard` with `guest:codex` and a task carrying the
  card id and `Verified-By:`, the single `board_comment` of kind `progress` with **no** move, the
  `board.verify` hint, and a card with no `qa` (and a card outside a QA lane) offering nothing.
