---
id: J0VY
type: work
status: needs-verification
labels: [bug, performance]
assignee: agent
implemented_by: glm/glm-5.3
session: 0806c7fd-6b22-4d1d-ac66-ec3e1ea8f5bb
rank: zzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: pane 0806c7fd, 2026-09-24
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-j0vy-n6y3-catalog-echo-and-math-gate/], related: [], github: null}
---
# GUI echoes app_catalog to every worker in a self-sustaining loop, ~50k events in 3h, main thread 15-30% CPU idle

## Issue
the equation is making relay run slow. do you know what would case that?
While investigating "the equation is making relay run slow" (2026-09-24), the equation was exonerated by measurement — instead the GUI is stuck in a self-sustaining settings-catalog echo loop that keeps the main thread busy continuously. This is the likely cause of the sluggishness the user felt.

**Measured evidence (2026-09-24, pid 3747251, up 14h):**

- `~/.local/share/relay/logs/relay.log` holds **50,185 `app_catalog_updated` events between 14:56Z and 18:27Z** (one batch roughly every 8 s; each batch is one broadcast echoed by ~37 pane workers, logged once per pane).
- Hottest minutes: 18:18Z→690 and 18:19Z→672 lines — exactly when the user's pane was streaming the reply they blamed (14:18 EDT). The loop pre-dates that reply (already present in the first lines of the rotated log, 10:56 EDT).
- Live samples of the otherwise-idle app: **15–30% CPU on the GUI main thread** (tid 3747251; per-thread /proc sampling, 2026-09-24 14:2x EDT). No latex/rsvg/render-math processes running; the equation's total footprint is one 0.6 s async, content-hash-cached LaTeX render plus a small tinted PNG per paint.
- Adjacent churn from the same loop: `index.db-wal` at 53 MB and being written every second.

**Mechanism (code):**

1. A worker sends a `presets` event (worker.py `emit_presets`, also re-fired by its own listeners: `openrouter_catalog`, `provider_limits`, `guest_harness_provider`, `customproviders`, `relay_pro` — worker.py:221-230, backend/relay_core/worker.py).
2. GUI: `RelayWindowCore.cpp:722` handles a helper worker's `presets` by calling `SettingsWatch::instance().notify()`.
3. `RelayWindow.h:441`: the window's SettingsWatch listener runs `sendAppCatalog()`, which rebuilds `appCommands().catalog(tabIdOf(page))` for **every pane** and sends the full `app_catalog` blob to every pane worker *and* helper worker (src/RelayWindow.h:1690-1700, comment: "nothing is cached; each refresh resends the whole block").
4. Every worker applies it and echoes `app_catalog_updated` (backend/relay_core/app_tools.py:1556) — ~37 GUI events per broadcast, all on the main thread.
5. Presets/catalog listeners re-fire `emit_presets` on the workers, closing the loop (e.g. `m_localModels.onPresetsChanged → refreshPresets()` for every pane, src/RelayWindow.h:2192).

With ~37 workers the loop self-sustains at one full round trip every few seconds regardless of user activity, scaling linearly with pane count — and every pane opened makes it worse. It coincided with (but was not caused by) the LaTeX equation the user suspected.

## Done means
A `presets` event (or any other `SettingsWatch::notify()`) never resends an `app_catalog` blob whose content is byte-identical to the last one that worker received; a changed catalog still reaches every worker immediately; a worker that restarts still gets its initial catalog. Proved by a unit test on the gate and the existing suites staying green (`ctest -R appcommands`).

## Plan
Content-gate every `app_catalog` send on the GUI side — the one place every arm of the loop passes through — so an unchanged catalog sends nothing no matter which listener re-fires. No worker/backend changes (those files are contested by other sessions today).

1. `AppCommands::catalogChanged(QByteArray &lastSent, const QJsonObject &app)` (static, src/AppCommands.{h,cpp}): serialize compact, compare with `lastSent`, update on change. This is the brake and the unit under test.
2. `Pane::sendAppCatalog()` (src/Pane.h): gate the send on a new `m_lastAppCatalog` member; clear it wherever `m_configured` turns false (src/PaneRuntime.cpp:516, src/PaneEvents.cpp:797) so a restarted worker always gets its first catalog.
3. `RelayWindow::sendHelperCatalogs()` (src/RelayWindow.h): same gate per helper worker, stored as a property on the `BoardWorker` so it dies with the worker.
4. Test: `relay-appcommands-tests` (tests/appcommands_test.cpp) — first send passes, identical suppressed, changed passes, cleared (worker restart) passes.

Why not also gate the worker echo (`app_catalog_updated`): with the send gated, workers only receive real changes, so echoes only happen on real changes; touching backend/relay_core today would collide with another session's uncommitted work.

## Execution Summary
Implemented as planned (commit `fc6c5cce` on main):

- `AppCommands::catalogChanged(QByteArray &lastSent, const QJsonObject &app)` — serializes the catalog compactly and compares it with the blob that worker last received; identical content sends nothing and leaves `lastSent` alone.
- `Pane::sendAppCatalog()` gates its send on a new `m_lastAppCatalog` member, declared beside the worker-lifecycle state and cleared in the worker `started` handler (src/PaneRuntime.cpp) so a fresh worker always receives its first catalog. The `configure` request also still embeds the catalog, so a restarted worker is covered twice.
- `RelayWindow::sendHelperCatalogs()` applies the same gate per helper worker via a `relayLastAppCatalog` property, which dies with the worker object.
- No backend changes.

Landing note: the working tree holds a concurrent session's uncommitted #SMDX hunks in `src/AppCommands.cpp` (3) and `src/Pane.h` (2); the commit selected only this card's hunks (`land.py --only-hunk`), the verify build built the exact landed tree, and those hunks remain uncommitted in the tree for their session.

The running Relay process still has the old code — the flood continues until the app restarts; after restart, `app_catalog_updated` should appear in relay.log only when a setting actually changes.

## Tests
- `relay-appcommands-tests` slot `catalogChangedGatesIdenticalResends`: first send passes, identical content suppressed, changed content passes, cleared cache (worker restart) passes. Full suite: 60 passed, 0 failed.
- `ctest --test-dir build -R 'markdown|appcommands'` green (2/2) on the landed code.
- `scripts/relay-build --target relay` clean; land.py's verify build compiled the exact tree it put on main.
- Evidence: docs/qa_evidence/2026-09-24-j0vy-n6y3-catalog-echo-and-math-gate/

## Try it
docs/qa_evidence/2026-09-24-tryit-J0VY/stage.sh

The script opens a disposable Relay (the fixed binary) with eight panes, lets it idle a minute, and prints how many `app_catalog_updated` events that produced — then leaves the window open for you. Type and scroll in it a little; your own Relay window, which still runs the old code until restarted, makes a fair side-by-side comparison.

After a minute of playing with it — did it stay as responsive as you expect, and did anything about the panes or the printed count surprise you? (~3 min)

Expected: docs/qa_evidence/2026-09-24-tryit-J0VY/expected.md (sealed until you answer)
