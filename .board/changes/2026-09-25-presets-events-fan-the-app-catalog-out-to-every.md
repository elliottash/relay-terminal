---
id: BT7C
type: work
status: needs-verification
labels: [bug, performance]
assignee: claude-opus-5-5
implemented_by: anthropic/claude-opus-5-5 via claude-code
rank: zzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
links: {plans: [], commits: [69eb1565ec78], evidence: [.board/changes/2026-09-25-presets-events-fan-the-app-catalog-out-to-every.md], related: [], github: null}
---
# presets events fan the app catalog out to every worker; GUI main thread idles at 50–70% CPU

## Issue
Relay's GUI main thread runs 50–70% CPU continuously, making the app feel sluggish. Every worker `presets` event calls SettingsWatch::notify(), which rebuilds the options/actions catalog and sends it to all ~26 workers; each worker applies it and emits `app_catalog_updated` back. Bursts of ~21 events arrive every 6–10 s all day, so the main thread never idles. The #J0VY content gate never suppresses because catalog() embeds live row values, and usage figures ride those rows since #V1VM/#KQNP/#JX8Z (the newest, e07024d7, landed 2026-09-25 00:27 — matching "worse today").

> "can you look at my machine and relay now. it feels sluggish. are you able to tell what is doing that? it feels liek relay is more sluggish today than yesterday so i wonder if soemthing changed. we can run profiling / performance tooling"
> — elliott · [session:38881d303fa344d7a078e1d28e41ae8c](relay://session/38881d303fa344d7a078e1d28e41ae8c) · 2026-09-25

## Evidence
Measured 2026-09-25 ~13:56–14:05 local, relay pid 499902 (restarted 13:40 after a build; binary = today's code):

- GUI main thread 54–70% CPU sustained (`top -H`, all other threads idle; utime delta 126 ticks / 2.5 s ≈ 50% userspace). Not memory/IO: MemAvailable 110 G, SwapFree = SwapTotal, PSI cpu/memory ≈ 0, 20 cores 66% idle (3–6 `cc1plus` from other sessions' builds take the rest).
- `~/.local/share/relay/logs/relay.log`: 4,978 `app_catalog_updated` in the last 20k lines (~26 min); every one of ~21 panes emitted exactly 99; bursts of ~21 every 6–10 s. Rate tracks activity: 325/h at 05:00 → 2,011/h at 11:00 → 4,158/h at 13:00. Also in that window: 669 `presets`, 749 `usage_limits`, 1,057 `usage` events. 23+ worker processes.
- Chain: worker `presets` event → `src/PaneEvents.cpp:243` `SettingsWatch::instance().notify()` → `src/RelayWindow.h:1712` `sendAppCatalog()` rebuilds `catalog(tab)` (`src/AppCommands.cpp:463`) for every pane, serializes and sends → each worker applies and emits `app_catalog_updated` (`backend/relay_core/app_tools.py:1575`) → GUI handles all of them.
- Why the #J0VY content gate doesn't help: `catalog()` embeds every row's live value (`rowValue`), and usage figures ride those rows since #V1VM (f607e5e9, 09-23 23:38), #KQNP (22464f33, 09-24 00:06) and #JX8Z (e07024d7, 09-25 00:27 — "Record subscription usage states and refresh routing scores", the newest, landing the morning the sluggishness was reported). With active agents the values change on every rebuild, so the bytes never compare equal and every send reaches every worker.
- Profiling limits: perf_event_paranoid=4 and yama ptrace_scope=1, so no perf/gdb attach; diagnosis is /proc sampling plus the event census above.
- Secondary, not the cause: 3–6 concurrent compile jobs from other sessions' land.py verify builds (machine stays 66% idle); "Protocol error (AttributeError)" spam earlier today was f6432615, fixed 13:54.

## Execution Summary
Three changes, root cause first:

1. **Stable catalog bytes.** `SettingRow::agentDetail` (`src/SettingsPane.h`): when set, `AppCommands::catalog()` carries it instead of `detail` (option entries and their button actions). Options › Models provider rows (`src/RelayWindowModels.cpp`) set it to their status without `limitsText(...)` ("5h 62% left · resets in 2h"), which changed on every usage event and every minute. With it gone the #J0VY byte gate holds again, so an unchanged catalog reaches no worker and draws no `app_catalog_updated` echo. Options still draws the live figures.
2. **Build once per tab per fan-out.** `RelayWindow::sendAppCatalog()` keeps a per-tab cache for the duration of the fan-out (`appCatalogForTab`), so ~26 panes cost one catalog build per tab instead of 26 full Options builds, sent or not. Outside a fan-out the catalog is built fresh as before.
3. **Coalesce.** The SettingsWatch listener now calls `scheduleAppCatalog()`: a 500 ms single-shot timer, not restarted by later notifies, so a burst of `presets` events from many workers is one fan-out and a steady stream still flushes twice a second.

Test: `tests/appcommands_test.cpp::theCatalogCarriesTheStableDetailNotTheLiveFigures` (catalog uses agentDetail in the option and its action; two builds differing only in the live figure pass the gate as unchanged). `ctest -R appcommands` passes. The shared `build/` currently fails in `RelayWindow.h` on #3B1B's in-progress `ContextDock` hunks (not in this commit); land.py's verify slot built the exact landed tree.

## To verify
Restart Relay with ~20+ panes and agents active. Expect: `grep -c app_catalog_updated ~/.local/share/relay/logs/relay.log` over 10 min to drop from ~1,900 (the 26-min rate was 4,978) to near zero while no setting changes; the GUI main thread (`top -H -p <relay pid>`) to idle well below the 50–70% measured; Options › Models to still show the usage figures on provider rows.
