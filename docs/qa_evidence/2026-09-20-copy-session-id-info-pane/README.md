# The ⓘ pane's copy affordance — implementer evidence (#YQC3, 2026-09-20)

`drive.sh` boots Relay under Xvfb with an isolated `HOME`, `XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR`
and `TMPDIR`, points the pane's provider at `stub-provider.py` (loopback, OpenAI-compatible SSE;
the harness of the transcript-gap run #5AWD). The stub's one turn emits an `agent` tool call, so
the session history holds a **real subagent thread** and the thread page is a real one. Scenes
drive the pane with `/status` (twice), click through OCR- and pixel-located coordinates, and read
the clipboard back with `xclip -o`. OCR of the shots is in `implementer-notes.txt`; the worker
logs under `logs/` (worker-faults.log empty), the app's stderr as `relay.log`.

The change (`src/SessionInfo.{h,cpp}`): the Session row's id and a ⧉ icon (U+29C9, small and
muted) share one `relay-info:copy` anchor — either click puts the id on the clipboard and flashes
the pane's bottom hint line, which restores its standing text on a 2 s single-shot timer. The
thread page's Thread row gets the same pair.

## The run's exact bytes

- saved session file: `…/relay/sessions/<dir>/bf688cfb2f664642aa0b6a6a6f065b62.json` —
  session id `bf688cfb2f664642aa0b6a6a6f065b62`; thread file
  `…/bf688cfb….threads/b7b8e79debd04757b06e580d8fd74f4f.json` — thread id
  `b7b8e79debd04757b06e580d8fd74f4f`.
- click the id text (x=857, mid-token) → clipboard `bf688cfb2f664642aa0b6a6a6f065b62` (equal,
  byte for byte).
- ⧉ ink cluster right of the id: x 996–1005; click x=1000 — on the glyph's own pixels — →
  clipboard `bf688cfb2f664642aa0b6a6a6f065b62` again.
- thread page ⧉ cluster x 974–983; click x=978 → clipboard
  `b7b8e79debd04757b06e580d8fd74f4f`, equal to the saved thread file's id.

| Shot | Claim |
| --- | --- |
| `implementer-01-noid.png` | `/status` before any turn: the Session row already carries the (pre-first-turn) id and the ⧉; the file row says "(not saved yet: nothing is written before the first turn)". No dead link anywhere. |
| `implementer-02-id.png` | After the turn: Session row `bf688cfb…5b62` + ⧉, `Turns 1 · 1 subagent thread`, history thread link, `Instructions none loaded`. `-zoom.png` is a 300 % crop of the row: the ⧉ draws as two joined squares (FreeSerif, which covers U+29C9 — fontconfig's pick, see notes — so no tofu). |
| `implementer-03-copy-id.png` | Within the flash window after clicking the id text: bottom line reads "Copied session id to the clipboard". |
| `implementer-04-restored.png` | 2.5 s later the standing line is back: "Links open subagent threads · Alt+Left back · F5 refresh · Esc closes · Alt+I opens this". |
| `implementer-05-copy-icon.png` | Click on the ⧉ glyph itself (x=1000, the ink cluster): the same exact id on the clipboard, "Copied session id…" again. |
| `implementer-06-thread.png` | The history's thread link opens the thread page in the same view: "↑ owner session: …", `Thread b7b8e79d…4f4f` + ⧉, its file, history. |
| `implementer-07-copy-thread.png` | Click on the thread page's ⧉ (x=978): "Copied thread id to the clipboard", clipboard `b7b8e79debd04757b06e580d8fd74f4f`. |

## Two things the run taught

- **Clicking past a line's last char can still fire its anchor** — Qt snaps the cursor back to
  the line end. An earlier pass "proved" the icon with a click 14 px into blank background
  (end-of-line snap, not the glyph). `glyph_x` in `drive.sh` now finds the ⧉'s ink cluster and
  the scene clicks that, so the evidence lands on the glyph's own pixels.
- **A live session always has an id** — the worker assigns it before the first turn, so the
  "empty id renders no anchor or icon" case cannot be shown live (scene 01 shows the closest
  live state: id present, file not saved yet). That case is covered by the unit test
  (`infoRendersSessionAndThread` asserts `renderInfo` with no `session_id` emits no
  `relay-info:copy` href at all).

## Suites

`scripts/relay-build` clean; `ctest --test-dir build -R conversations` passes (the ⓘ tests ride
in `conversations_test.cpp`: the two anchors and their shared href, the percent-decode
round-trip, the empty-id case, and the `anchorClicked` → clipboard → hint flash wiring).
