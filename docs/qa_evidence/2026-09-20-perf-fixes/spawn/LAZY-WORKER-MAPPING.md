# #GMCF decision 1 — mapping the lazy agent worker, and why it stops here

Decision 1: keep one worker per pane (the owner: "220MB seems like a good trade for the isolation"),
but start it lazily, "so a pane used only as a terminal never pays ~27 MB" — **only if it is
invisible**. This is the map that was to come first. It says the premise does not hold in Relay's
input model, and that what is left to save is visible. Nothing was landed for decision 1.

Line numbers are `src/Pane.h` at `fc2a0683` unless another file is named.

## What starts today

`Pane`'s constructor runs `buildUi(); startWorker(); startTerminal();` (:472). The worker answers
`ready`, and from there a fixed chain runs with no user action at all — measured on a fresh profile
(`prof-notice/data/relay/logs/relay.log`, the run that produced `slow-probe-unisolated-notice.png`):

```
worker_start pane=08ede71d
event type=ready      pane=08ede71d          (t ≈ 0.49 s)
event type=configured pane=08ede71d model=relay-main   (t ≈ 0.60 s)
```

`ready` (:9560) → `requestRoute(false,"auto")`, `send presets`, `refreshAliases()`.
`presets` (:9631) → picks the preset and calls `configurePreset()` (:12437) → `configure`.
`configured` (:9614) → `m_model`, `m_skillCount`, `setSkillCommands()`, the role and tier summaries,
`onSessionConfigured()`, `noteGuestPreset()`, `discloseHosted()`, `runBoardTask()`, `changed()`.

## 1. The premise: a "terminal-only" pane is not worker-free

In Relay the prompt box *is* where terminal commands are typed ("Shell commands or agent prompts…"),
and the default input mode is `auto` (`defaultInputMode()`, :1449). In `auto` every submitted line
is routed **by the worker**: `requestRoute()` ends at `send({{"type","route"}, …})` (:9500-9513) and
the line is dispatched only when the `route` reply comes back (:9568-9613). The preview label does
the same on every keystroke: `textChanged` → `m_debounce` (150 ms) → `requestRoute(false,"auto")`
(:503-504), in every mode, including `shell`.

So the worker is not "the agent's process that a terminal user never touches" — it is the thing that
decides whether `git stauts` is a command or a sentence. The ~27 MB is only saved in a pane where
the user **never types in the prompt box at all**, not in a pane used as a terminal. And a pane
nobody has typed in yet is exactly the pane whose chrome the next section is about.

## 2. What the pane visibly shows because the worker configured

A lazily-started pane would differ, on screen, from the moment it opens:

| what | where | source |
|---|---|---|
| the model chip, e.g. `relay free (main)` | composer | `roleRowText()` :11085-11089 reads `m_currentPreset` / `m_model`, set only by `configurePreset()` :12449 and the `configured` event :9616 — with no worker it reads the bare role name |
| the Relay Free chip, `Free` / `Free · N% left` | composer | `updateQuotaLabel()` :4216 hides itself unless `onHostedPreset()`, i.e. unless a preset is configured |
| the Relay Free disclosure paragraph | printed into the terminal | `discloseHosted()` :4241, called from `configured`; it is a privacy notice, shown once per installation |
| the `/` command list and skills | prompt box | `setSkillCommands()` from `configured` :9618; `refreshAliases()` from `ready` :9562 |
| the route preview label (`TERMINAL · …`, `AGENT · …`) | composer | the worker's `route` reply, on every keystroke (above) |
| the first-run screens: instructions onboarding (+400 ms) and the approvals choice (+800 ms) | modal | `onSessionConfigured()` :4498-4510 — on a fresh install a lazy pane would show neither until an agent request was made |

`slow-probe-unisolated-notice.png` in this directory is a pane 2.6 s after it opened: the model chip,
the `Free` chip and the disclosure paragraph are all there, and all of them came from the worker.

## 3. The other things the trigger would have to cover

- **First focus is not a trigger.** Every pane takes the keyboard into its prompt box when it is
  created — twice, at 0 ms and 120 ms (`src/RelayWindow.h`:6667-6668, and :786 for a pane opened
  with state). "Spawn on first focus" would spawn every worker anyway, as the brief suspected.
- **A line submitted inside the spawn window is refused today, not queued.** With `!m_workerReady`,
  `requestRoute()` takes the `routerDown` path (:9434-9487): explicit `agent` and explicit
  `terminal` submits are dispatched locally (#N8VK), and an `auto` submit — the default — is
  refused with `relay::input::noRouterText(...)`. Making lazy start invisible means turning that
  deliberate refusal into a queue-until-ready, which is a change to the path #N8VK exists to
  protect.
- **`Pane::send()` drops what it cannot deliver** (:9321-9327): it writes when the process is
  `Running`, queues when it is `Starting`, and silently drops otherwise. A single `ensureWorker()`
  gate in `send()` would work — `QProcess::start()` makes the state `Starting` at once, so the
  existing `m_workerPending` queue covers the gap and `configure` still cannot be overtaken
  (#FEJQ) because it is sent from the `presets` reply, after `ready`.
- **A restored pane must stay eager.** `resumeRestoredSession()` (:4516) sends `resume`, and it is
  called from `onSessionConfigured()` — the end of the same chain. A restored conversation, its
  "Session loaded · N turn(s)" line and its recap all arrive through it. Gating on
  `m_restoreSession` / `m_initialState` / `m_restorePreset` is easy, so this is a condition, not a
  blocker.
- **Out of scope and unaffected**: the per-tab Switchboard and helper workers are separate
  processes started by `RelayWindow` (`src/BoardWorker.h`), guest harnesses (claude/codex) are
  started *through* `configure` and so need the pane worker anyway, and remote/phone sharing
  forwards this pane's worker events (`handle()` :9537 calls `relay::RemoteShare::paneEvent`).
- **Tests**: no C++ test constructs a `Pane` (it lives in the one `relay` translation unit); the
  Python tests drive `backend/worker.py` over stdio directly. Nothing in `tests/` asserts a worker
  at construction, so the tests are not what blocks this.

## 4. Verdict

Lazy start cannot be made invisible without restructuring, so per the brief nothing was landed.
What it would take, and why each part is the owner's call rather than a loose end:

1. **Decide what a not-yet-configured pane's composer shows.** Filling the model and Free chips
   without the worker means a local fallback for `m_presets` — which preset is usable, hosted or
   keyed — out of `relay::models` and `QSettings provider/preset`. That is a second source of truth
   for the thing the model-picker session is actively rewriting (`src/Pane.h` model box,
   `src/ModelCatalog.*`, `src/SettingsPane.*`), so it is theirs to shape, not a side effect of a
   memory fix.
2. **Decide when the Relay Free disclosure and the two first-run screens appear** if not when the
   pane opens. The disclosure is a privacy statement; moving it to "the first time you ask the
   agent anything" is a product decision.
3. **Decide whether an `auto` submit may queue** rather than be refused while the router is
   starting, reversing part of #N8VK.

And it is worth weighing against what is actually saved: with the prompt box routing every line,
the 27 MB comes back as soon as the user types, so the saving is the memory of panes nobody has
typed in — not of panes used as terminals.
