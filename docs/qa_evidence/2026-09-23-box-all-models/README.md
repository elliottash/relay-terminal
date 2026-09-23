# Card #BXMS — the model box picks; the Models pane only edits settings

Captured with `drive.sh` (sandboxed Relay on Xvfb) from the build `land.py` verified for commit
`12ef73d7`: `RELAY_BIN=/tmp/claude-1000/land/box-settings/verify/build/relay bash drive.sh`.

| Capture | What it shows |
| --- | --- |
| [01-box.png](01-box.png) | Alt+M: the classes, then **all models** and **model settings** at the foot. |
| [02-all-models.png](02-all-models.png) | "all models": the same box on every class whole plus **other models**, scrollable. |
| [04-picked-from-all.png](04-picked-from-all.png) | Typing `gpt-6-astra` in that list and Enter: the pane's chip is now gpt-6-astra. |
| [03-model-settings.png](03-model-settings.png) | "model settings": the Models pane. Its header reads "settings for every pane · a pane's own model is picked in its model box"; there is no "use" button, and the filter no longer says "enter uses it". |

Tests (on the exact landed tree): `relay-modelrows-tests` 23/23, `relay-modelspane-tests` 23/23,
`relay-modelpicker-tests` 54/55. The one failure, `theFooterNamesTheRealKeys`, is failing at HEAD
before this change. It came in with `e326182d` (#N4PW), which landed another session's split of the
priorities footer without that session's matching test edit, which is still uncommitted
(`codex-model-ties`).
