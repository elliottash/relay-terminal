# H6VQ — thread

<!-- relay:entry 20260921T033000Z-a1 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 23:30
Filed from the owner's report and claimed. Reading `~/.local/share/relay/logs/worker.log`
(≈2448–2456) and `relay.log` around 23:21:30–23:22:20 first, read-only.

<!-- relay:entry 20260921T041500Z-b2 author=claude-code kind=evidence -->
### Claude Code · 2026-09-21 00:15
Root cause, from the owner's logs and the code: `RelayWindow::boardWorker`'s `onEvent` matched the
event's name against `type`, and a worker names its events in `event`. The helper's `app_command`
was therefore never answered — since `c78c8004`, the day the helper route was written. In the
owner's run the call was still in flight when the tab closed, which stops a tab's helper (§30.7):
`app_open … ok=False ms=5358` beside `worker_stop`, and a turn that never answered.

Landed `fd4cb3c1` (the answer, and `app_open {target: "conversation"}` with `id`/`ids`, the brief,
the prompt section, the narration floor, §30.3/§30.4), `75af8a42` (a worker that dies mid-turn puts
its panels back; `board_chat_cancelled` carries the pane that pressed Stop) and `3eebe40a` (the
queue drawn in every pane). Evidence, 12 PASS 0 FAIL plus the before-fix reproduction through the
real worker: `docs/qa_evidence/2026-09-20-helper-opens-sessions/`. Moved to needs-verification.

<!-- relay:entry 20260921T052000Z-c3 author=claude-code kind=evidence -->
### Claude Code · 2026-09-21 01:20
Re-ran the live drive against the binary with all five commits in: 12 PASS, 0 FAIL. Added
`445869e9` — a queue row is read from any panel's state block, because one worker holds one FIFO
for the whole tab — and protocol §30.7 now states the three rules this card settled: the queue is
every panel's, `board_chat_cancelled` carries the pane that pressed Stop, and a worker that dies
mid-turn puts its panels back.

<!-- relay:entry 20260921T061804Z-xz author=claude-code kind=note -->
### Claude Code · 2026-09-21 06:18
Card #AGNT supersedes this card's panel-era UI. `relay::HelperChatPanel`, `BoardChatPanel`,
`relay::helpermodel` and the `board_chat*` messages are deleted (`db186adc`): a helper agent is
the prompt box a terminal pane has -- a no-shell `Pane` with a `relay::agent::Context` -- in the
Switchboard, on a card, and in Options, Actions and Sessions. What this card asked for is still
true and is now true in one widget rather than two: the queue in every panel.

<!-- relay:entry 20260922T005810Z-gd author=agent kind=note pane=switchboard -->
Handoff: done, needs-verification. A helper (Sessions especially) can open one or several saved conversations in new panes and says so in text; a dead helper worker resets its panel instead of hanging; the §12 queue and Stop work from every embedded panel, not just the Switchboard's. QA checklist is a 7-item walk; nothing left to build.
