# The cleanup button, in the pane (2026-09-18)

Implementer evidence for the **GUI half** of `#KDK9` — `README.md` beside this file is the backend
half. Design: `docs/SWITCHBOARD-DESIGN.md` section 4.8. Contract:
`docs/AGENT-SESSIONS-PROTOCOL.md` section 19.9. Code: `BoardView` in `src/BoardPane.cpp`.

These are the implementer's shots, not a QA verdict.

## How this was run

`cmake --build <build>`, then `build/relay --workspace <copy>` under this session's own **Xvfb
`:141`** (1700x1000, no window manager), window 1500x950, with its own `XDG_CONFIG_HOME`,
`XDG_DATA_HOME`, `XDG_CACHE_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR`. Driven with `xdotool`, captured
with `import -window <id>`, the pointer parked in the title bar (a tooltip is its own X window and
a window grab paints it black).

**The workspace is a throwaway git repo with seven cards** copied out of this repository's
`issues/` tree, plus its `board.yaml` — small on purpose, so a live run is short and cheap. This
repository's own `issues/` was never opened by the app.

**One live run, on a real provider.** `DBUS_SESSION_BUS_ADDRESS` was left pointing at the real
session bus so the desktop keyring could answer; everything else was isolated. The preset is
`glm-coding` (main model `glm-5.3`), set in the isolated `relay.conf`. No key was printed, logged
or captured. Two runs were started in all: one **dry run** carried to its summary (176 s, 7
proposals, 0 writes — `implementer-gui-dry-run-changelog.md` is the changelog it wrote, copied out
of the scratch workspace), and one started again only to photograph the running state and then
**stopped with the button** at 23 s. `Apply` was never pressed, so nothing the agent proposed was
ever written.

## The shots

| File | Shows |
|---|---|
| `implementer-gui-01-clean-up-button` | The board on open: **Clean up** in the list page's tool row, beside **+ New card**. |
| `implementer-gui-02-running-with-stop` | A run going. The same button now reads **Stop** in the warning colour, and the board's notice says `Cleanup preview · 0:09 · Requesting model · step 3/50 · nothing is written` — elapsed, the step, and the standing reminder that a preview writes nothing. |
| `implementer-gui-03-busy-on-a-card` | "Ask the agent" pressed on a card while the run is going. The pane stops it before it is sent and says why on the card; the typed message is back in the reply box, unsent. The progress line has moved clear of the refusal line, the reply box and the Ask button — it stays up for minutes, so it must not sit on the controls. |
| `implementer-gui-04-dry-run-summary-with-apply` | The dry run's summary, in the list page under the tools: *Cleanup preview — nothing was written · 7 cards before, 7 after · 7 proposed · 6 update · 1 comment · 2:56*, every proposal with its `#ID` as a link, and **Apply** / **Changelog** / **Dismiss**. |
| `implementer-gui-05-stopped-summary` | A run stopped with the button: *… · stopped · 0 proposed · 0:23*, the agent's report as far as it got, no **Apply** (half a plan is not a plan) and no **Changelog** (a run that wrote nothing writes none). |
| `implementer-gui-06-a-card-id-opens-the-card` | A `#ID` in the summary clicked: it opens that card through the same path a row click does. |

## Honest notes

- `implementer-gui-04` was captured from the first launch, before two later fixes that do not touch
  the panel: where the progress line is placed over an open card, and clearing the "a cleanup is
  running" line from a card when the run ends. Both are covered by tests in
  `tests/boardmodel_test.cpp`.
- The scratch board ends with one uncommitted change, `#GDQN` `ready` → `in-progress`. **That was
  the driver, not the agent**: an `xdotool` `Tab` landed on the card's status picker and the typed
  words prefix-matched "In progress" in the combo box. The card's own thread records it as
  `owner moved this card`, and the dry run's changelog records 0 writes.
- **Apply was not exercised against a real run** — pressing it would have let the agent rewrite the
  scratch cards for real, which is more provider time than this needed. What it sends is covered by
  `applyingAPreviewRunsTheCleanupForReal` in `tests/boardmodel_test.cpp`.
- Nothing scripted the events: every shot above is a real worker answering a real message.
