# #G2C7 Try-it staging notes — 2026-09-26

## What was staged

`stage.sh` (beside this file) builds a disposable project under
`/home/elliott/.cache/relay/scratch/tryit/g2c7/` — `proj/` with a seeded `.board/`
(three cards: **G2C7** claimed now *and* once before, **MDSG** idle, **Q1W2** whose
body alone mentions "sorting") and five seeded sessions under an isolated
`RELAY_DATA_DIR`, all in the project's workspace, titled so every behaviour has a
target: "sortable columns" (exact), "sorting the sessions table" (word start),
"sessions table recap" (contains), "unrelated bake-off" (full-text only — its body
says "the sorting must be instant, not a sqlite wait"), "board parity notes"
(mentions #G2C7).

## What could not be staged, and why

The Relay binary refused to run as a second, isolated instance on this machine:
with `HOME`/`XDG_*`/`RELAY_DATA_DIR` all pointed at the sandbox it starts, writes
its open-socket pointer, and exits without a window (empty log, dead process,
`ss` shows no listener). A `relay-drive panes` that followed the stale pointer was
answered by a *different* live instance — read-only, nothing was typed into or
opened in anyone's window, and the pointer was deleted. Driving the owner's real
window is not something this staging may do, so the staged app pass was abandoned
rather than retried against the wrong instance.

Two harness limits are worth recording for whoever builds the next stage:
`relay-drive type` reaches only five Board fields, so the Sessions search box has
no named seam; and the sessions data root (`RELAY_DATA_DIR`) works, but the
single-instance registration does not respect it.

## The user's path, numbered

1. **check** — the suites named in the card's `## Tests`: `tests.test_conv_index`
   (132 OK), `build/relay-conversations-tests` (59 passed; 1 pre-existing failure
   filed as #TJJ3), `build/relay-boardfilter-tests` (6 passed).
2. **agent** — *not played*: the app would not run isolated (above). The
   behaviours this step would have eyeballed are covered by the headless tests in
   step 1 and by the owner's own pass in step 3.
3. **person** — the owner, in their own Relay (already built from `main`):
   open Sessions (`sessions.open`), and judge the four asks — the columns back
   under a straddling title; typing `sorting` (or three letters of any word)
   answering before the SQLite pass with the matched words washed; `#g2c7`
   filtering instantly with chips before the snippet, claimed ones boxed and
   bold; and the Board filter answering the same way (exact "Sorting the list"
   first, "sorting" washed, the bake-off card showing its matched line).
