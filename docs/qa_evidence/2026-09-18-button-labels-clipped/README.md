# Implementer evidence — button labels painted past the button's edge

Issue: [`issues/changes/needs_qa_llm/2026-09-18-button-labels-clipped.md`](../../../issues/changes/needs_qa_llm/2026-09-18-button-labels-clipped.md)
Implemented by Claude Opus 5 (Claude Code, coordinator session), 2026-09-18. These are implementer
captures, **not** a QA verdict.

| File | What it shows |
|---|---|
| `implementer-before.png` | The API keys modal's button row in the live stylesheet with the old `QPushButton:default { font-weight: 600 }` rule restored. `Add / replace…` is clipped at both ends, matching the owner's screenshot. |
| `implementer-after.png` | The same row after the change: the label reads in full with even padding. |
| `implementer-buttonfit-before.txt` | `relay-buttonfit-tests` against the old rule: 80 passed, 37 failed. Every failure is a `:default` row; no `plain` or `#primary` row fails. |
| `implementer-buttonfit-after.txt` | The same test after the change: 117 passed, 0 failed. |

## Reproducing

The renders come from a throwaway harness that lays out the modal's four buttons under
`relay::theme::applyTheme()` and renders the dialog offscreen, with `before` appending the old rule
to the application stylesheet. The test logs need no harness:

```sh
cmake --build build --target relay-buttonfit-tests
# after (the committed state)
QT_QPA_PLATFORM=offscreen ./build/relay-buttonfit-tests
# before (the reported bug, without editing the stylesheet)
QT_QPA_PLATFORM=offscreen RELAY_BUTTONFIT_EXTRA_QSS='QPushButton:default { font-weight: 600; }' \
    ./build/relay-buttonfit-tests
```

Both logs here were captured with `propagateSizeHints` warnings from the offscreen platform plugin
filtered out; nothing else is edited.
