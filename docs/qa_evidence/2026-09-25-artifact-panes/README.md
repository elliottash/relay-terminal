# #PBZ4 live pass — the agent docked under a file editor

2026-09-25. Relay built from a clean `git archive` of `a0041445` plus this card's two fixes below
(`src/FilePanes.cpp`, `backend/relay_core/board_chat.py`), run under its own Xvfb display with an
isolated HOME and `RELAY_DATA_DIR` pointed at that export. The model is `stub-provider.py`, a local
OpenAI-compatible endpoint: "add a sentence" makes it wait 12 s and then call `edit_file` on the
scratch project's `README.md` (a fresh git repo, not the checkout's README). `drive.sh` replays it.

| Shot | What it shows |
|---|---|
| `00-before-fix-tab-key-refused.png` | **Defect 1, as found.** The first expand of the docked agent: "tab must be the tab's id, at most 64 characters." The worker took the artifact console's persist key `<tab>/file:<path>` for the tab id (`board_chat.tab_of` knew only the card key's shape), so `configure` was refused and the console never configured. |
| `00b-before-fix-no-plugin-actions.png` | **Defect 2, as found** (after fixing 1). The console configures, but the action row is Save and Revert only: the first file of a session is opened in `ToolPane`'s constructor, before `createToolPane` sets where plugins live, so its plugin was looked up against no search path. |
| `01-docked-agent-with-markdown-actions.png` | Fixed. `README.md` in edit mode, the "README.md agent" console at the foot with the `relay.markdown` actions Outline (o), Tighten (t), Proofread (p), Toc (c), then Save (s), Revert (r); the placeholder says "or / for Markdown commands". |
| `02-slash-popup-markdown-commands.png`, `02b-slash-popup-filtered.png` | `/` opens the popup with Relay's rows first; `/o` puts `/outline ✦ Markdown · Summarise the document's structure, heading by heading.` at the top. |
| `03-typing-during-the-turn.png` | The request sent; while the turn runs, " Typed while the agent worked." is typed on the Notes line. |
| `04-sentence-landed-typing-kept.png` | The agent's `edit_file` landed in the buffer: "The docked agent added this sentence." under Usage, the typing on the Notes line intact, the agent bar "Edit README.md (+2 -0) · lines 9–10 · unsaved", the dock "1 · change 1 turn". The tool result (`stub-requests.log`) is `open_buffer: {applied: exact, saved: false}`: the buffer was dirty, so the worker patched the unsaved text rather than the disk, and the disk stayed the original. |
| `05-change-list-names-the-turn.png` | The change list: `Turn · "Please add a sentence under Usage."`, then `lines 9–10 · Edit README.md (+2 -0) · unsaved`. |
| `06-one-undo-removes-only-the-agents-step.png` | One Ctrl+Z in the editor: the sentence is gone, "Typed while the agent worked." is still there — the agent's change was one undo step. |
| `07-outline-action-runs.png` | The Outline button sends the plugin's prompt as an ordinary ask; the answer ends "• Notes: open items" in the console. |

The card's Verify line — the sentence appears as one undo step, the typing survives, `/` shows the
Markdown plugin's actions, the change list names the turn — holds in 04, 06, 02b and 05.

Both defects were fixed with a test each: `tests/test_board_chat.py`
(`test_an_artifact_consoles_key_names_a_file_not_the_tab`) and `tests/filesync_test.cpp`
(`aFileOpenedBeforeThePluginSearchGetsItsPluginWhenTheConsoleIsBuilt`, which fails without the fix).
