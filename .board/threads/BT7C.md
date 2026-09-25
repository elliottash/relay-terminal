<!-- relay:entry 20260925T180137Z-dr author=agent kind=event model=k3 pane=3fc19f58 turn=38881d303fa344d7a078e1d28e41ae8c/d3584994e9734938ae30c24108149b97 -->
- ✦ agent created this card in Discussing · .board/changes/2026-09-25-presets-events-fan-the-app-catalog-out-to-every.md

<!-- relay:entry 20260925T180148Z-a8 author=agent kind=event model=k3 pane=3fc19f58 turn=38881d303fa344d7a078e1d28e41ae8c/d3584994e9734938ae30c24108149b97 -->
- ✦ agent updated this card · appended to `## Evidence`

<!-- relay:entry 20260925T180157Z-rs author=agent kind=event model=k3 pane=3fc19f58 turn=38881d303fa344d7a078e1d28e41ae8c/d3584994e9734938ae30c24108149b97 -->
- ✦ agent updated this card · replaced `## Evidence`

<!-- relay:entry 20260925T180157Z-rt author=agent kind=rewrite model=k3 pane=3fc19f58 turn=38881d303fa344d7a078e1d28e41ae8c/d3584994e9734938ae30c24108149b97 -->
- ✦ rewrote ## Evidence

<details><summary>before</summary>

```
Measured 2026-09-25 ~13:56–14:05 local, relay pid 499902 (restarted 13:40 after a build; binary = today's code):

- GUI main thread 54–70% CPU sustained (`top -H`, all other threads idle; utime delta 126 ticks / 2.5 s ≈ 50% userspace). Not memory/IO: MemAvailable 110 G, SwapFree = SwapTotal, PSI cpu/memory ≈ 0, 20 cores 66% idle (3–6 `cc1plus` from other sessions' builds take the rest).
- `~/.local/share/relay/logs/relay.log`: 4,978 `app_catalog_updated` in the last 20k lines (~26 min); every one of ~21 panes emitted exactly 99; bursts of ~21 every 6–10 s. Rate tracks activity: 325/h at 05:00 → 2,011/h at 11:00 → 4,158/h at 13:00. Also in that window: 669 `presets`, 749 `usage_limits`, 1,057 `usage` events. 23+ worker processes.
- Chain: worker `presets` event → `src/PaneEvents.cpp:243` `SettingsWatch::instance().notify()` → `src/RelayWindow.h:1712` `sendAppCatalog()` rebuilds `catalog(tab)` (`src/AppCommands.cpp:463`) for every pane, serializes and sends → each worker applies and emits `app_catalog_updated` (`backend/relay_core/app_tools.py:1575`) → GUI handles all of them.
- Why the #J0VY content gate doesn't help: `catalog()` embeds every row's live value (`rowValue`), and usage figures ride those rows since #V1VM (f607e5e9, 09-23 23:38), #KQNP (22464f33, 09-24 00:06) and #JX8Z (e07024d7, **09-25 00:27**,
```

</details>

<details><summary>after</summary>

```
Measured 2026-09-25 ~13:56–14:05 local, relay pid 499902 (restarted 13:40 after a build; binary = today's code):

- GUI main thread 54–70% CPU sustained (`top -H`, all other threads idle; utime delta 126 ticks / 2.5 s ≈ 50% userspace). Not memory/IO: MemAvailable 110 G, SwapFree = SwapTotal, PSI cpu/memory ≈ 0, 20 cores 66% idle (3–6 `cc1plus` from other sessions' builds take the rest).
- `~/.local/share/relay/logs/relay.log`: 4,978 `app_catalog_updated` in the last 20k lines (~26 min); every one of ~21 panes emitted exactly 99; bursts of ~21 every 6–10 s. Rate tracks activity: 325/h at 05:00 → 2,011/h at 11:00 → 4,158/h at 13:00. Also in that window: 669 `presets`, 749 `usage_limits`, 1,057 `usage` events. 23+ worker processes.
- Chain: worker `presets` event → `src/PaneEvents.cpp:243` `SettingsWatch::instance().notify()` → `src/RelayWindow.h:1712` `sendAppCatalog()` rebuilds `catalog(tab)` (`src/AppCommands.cpp:463`) for every pane, serializes and sends → each worker applies and emits `app_catalog_updated` (`backend/relay_core/app_tools.py:1575`) → GUI handles all of them.
- Why the #J0VY content gate doesn't help: `catalog()` embeds every row's live value (`rowValue`), and usage figures ride those rows since #V1VM (f607e5e9, 09-23 23:38), #KQNP (22464f33, 09-24 00:06) and #JX8Z (e07024d7, 09-25 00:27 — "Record subscription usage states and refresh routing scores", the newest, landing the morning the sluggishness was reported). With active agents the values change on every rebuild, so the bytes never compare equal and every send reaches every worker.
- Profiling limits: perf_event_paranoid=4 and yama ptrace_scope=1, so no perf/gdb attach; diagnosis is /proc sampling plus the event census above.
- Secondary, not the cause: 3–6 concurrent compile jobs from other sessions' land.py verify builds (machine stays 66% idle); "Protocol error (AttributeError)" spam earlier today was f6432615, fixed 13:54.
```

</details>

<!-- relay:entry 20260925T180202Z-nv author=agent kind=question model=k3 pane=3fc19f58 turn=38881d303fa344d7a078e1d28e41ae8c/d3584994e9734938ae30c24108149b97 -->
Which fix do you want? My recommendation: coalesce `sendAppCatalog()` fan-outs on a trailing 5–10 s timer in `SettingsWatch::notify()` (one send per quiet period instead of one per presets event), and separately stop the #J0VY gate being defeated by comparing a hash over the static schema plus values that only change when a human touches a row — volatile usage figures should not re-serialize a catalog. Alternatives: send the catalog only to panes whose own presets changed, or move serialization off the GUI thread.

<!-- relay:entry 20260925T180202Z-nw author=agent kind=event model=k3 pane=3fc19f58 turn=38881d303fa344d7a078e1d28e41ae8c/d3584994e9734938ae30c24108149b97 -->
- ✦ agent updated this card · waiting_on: (unset) → owner

<!-- relay:entry 20260925T181228Z-ad author=agent kind=decision model=claude-opus-5-5 pane=3fc19f58 turn=38881d303fa344d7a078e1d28e41ae8c/246365c1ce4f4557b4b58acf3ecb8b4f -->
"check the plan, and after any refinements, implement it" — elliott, 2026-09-25. Refinement: fix the volatility at its source (agentDetail) and build once per tab; a 500 ms coalesce instead of the 5–10 s timer, so agents never read stale settings for long.

<!-- relay:entry 20260925T181240Z-7c author=agent kind=event model=claude-opus-5-5 pane=3fc19f58 turn=38881d303fa344d7a078e1d28e41ae8c/246365c1ce4f4557b4b58acf3ecb8b4f -->
- ✦ agent updated this card · waiting_on: owner → (unset); assignee: (unset) → claude-opus-5-5; appended to `## Execution Summary`
