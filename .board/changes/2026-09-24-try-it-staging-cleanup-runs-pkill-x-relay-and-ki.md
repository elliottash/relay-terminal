---
id: 8YNJ
type: work
status: planned
labels: [bug, try-it]
rank: zzzzzzzzzzzzzzzzzzy
created: '2026-09-24'
source: pane 1, 2026-09-24 — traced from relay.log / worker.log / conversations DB after the app closed itself at 18:24 local
links: {plans: [], commits: [], evidence: [], related: [Z82M, 9JYK], github: null}
---
# Try-it staging cleanup runs `pkill -x relay` and kills the live desktop app

## Issue
why did it close a few minutes ago

`#Z82M` Try-it staging (agent pane `c5d9948d`, session `bb3324c2…`) launches a sandboxed Relay
under Xvfb (`DISPLAY=:42`, binary `/tmp/z82m-verify/build/relay`) and cleans up with `pkill -x
relay`. `-x relay` matches **every** process whose basename is `relay` — including the live desktop
app (`build/relay`) — so the cleanup takes the owner's session down with the sandbox.

Measured evidence (2026-09-24, `~/.local/share/relay/logs/` + conversations DB):

- 22:24:47.548Z `tool_started run_command` pane `c5d9948d` — conversations entry id 257820:
  `fuser -k 4761/tcp; fuser -k 4762/tcp; pkill -x relay 2>/dev/null; pkill -x Xvfb; sleep 2; …`
- 22:24:47.580Z `gui_quit reason=signal pid=2484272 uptime_s=2012 build=2026-09-24.17H.04` — the
  SIGTERM hit the live app 32 ms into that command. Graceful shutdown followed (every pane's
  `worker_exit reason=shutdown expected=1`); the current instance started 22:24:55Z (18H.04).
- The same pane ran the same cleanup at 21:50:59Z (entry id 252269), which ended the 21:47:54Z
  instance. It will recur with every Try-it verification round.

Not a crash: no `gui_crash` line, and #9JYK's signal-quit path did its job — layout and
scrollbacks saved; the pane's own tool call completed `ok=True` two seconds later (worker.log).

Fix that belongs in the staging procedure: never `pkill -x relay`. Target the sandbox by exact pid
file (`$(cat …/z82m/relay.pid)`), by full path (`pkill -f 'z82m-verify/build/relay'`), or via its
private `XDG_RUNTIME_DIR` — and the Try-it guidance should say so, so later cards do not inherit
the pattern.

## Done means
Try-it teardown never kills a process it did not start: the brief every Try-it turn receives forbids name-based kills (`pkill -x relay`, `pkill -x Xvfb`, `taskkill /IM`) and requires pid-file / full-path teardown, and the #Z82M staging carries an `unstage.sh` plus a rerunnable `stage.sh` that cleans up its own pid files, so no later round improvises.
Checked by: a second `stage.sh` run and then `unstage.sh` leave ports 4761/4762 free and the live desktop app's pid unchanged; targeted tryit tests pin the rule in the prompt on all three platforms. Failure would look like the incident again — a Try-it round's cleanup producing `gui_quit reason=signal` in `~/.local/share/relay/logs/` — or `rg 'pkill -x relay'` still finding a doc that teaches it.

## Plan
**Goal.** Try-it teardown must never kill a process it did not start. Two fixes: the brief every Try-it turn receives carries the rule, and the #Z82M staging gets a safe teardown so its next verification round cannot repeat the incident.

**Findings.**

- The dangerous command is in no repo script. The verifying agent improvised it: `docs/qa_evidence/2026-09-24-tryit-Z82M/stage.sh` starts only the fake provider and gateway (writes `$SANDBOX/fake.pid`, `$SANDBOX/gateway.pid` — lines ~53–58), never the app, and opens with `rm -rf "$SANDBOX"` while reusing fixed ports 4761/4762 — so a rerun cannot rebind until the old processes die. That vacuum is where `fuser -k …; pkill -x relay; pkill -x Xvfb` (conversations entry 257820) came from, SIGTERming the live app at 22:24:47.580Z.
- The guidance every Try-it turn receives is `backend/relay_core/board_tryit_brief.md`, loaded in `backend/relay_core/tryit_protocol.py` (`tryit_prompt()` → `_platform_brief()`, line 737): Linux uses it verbatim; macOS and Windows replace the `For the app:` span. Its staging bullets say nothing about teardown, and its pointer to the reference recipe doesn't name the cleanup half of that recipe.
- The right pattern already exists in the repo: `docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/ai-pass.sh:17` — `cleanup()` kills `$relay_pid`/`$xvfb_pid` recorded at launch, then removes the sandbox.
- Tests pinning the prompt: `tests/test_tryit_protocol.py` ("sealed" in prompt; ReuseTests), `tests/test_macos_tryit.py:41` (`assertNotIn('Xvfb', brief)`), `tests/test_tryit_windows.py:46,69`. None pins teardown text.

**Steps.**

1. **Brief rule (the durable fix).** In `backend/relay_core/board_tryit_brief.md`, add to the step-3 app block: the sandbox owns every process staging starts — write each pid (app, Xvfb, helpers) to `$SANDBOX/*.pid` at launch and provide an `unstage.sh` (or `stage.sh --down`) that kills exactly those pids, sweeping anything left by full path only (`pkill -f "/tryit/z82m"`-style) or the sandbox's private `XDG_RUNTIME_DIR`; **never kill by process name — `pkill -x relay` and `pkill -x Xvfb` match the owner's live app and other sessions' displays**. Keep Xvfb wording inside the `For the app:` span (that is what macOS/Windows replace) and add one platform-neutral line — kill by pid file recorded at launch, never by process name — to the macOS and Windows replacement blocks in `_platform_brief()` so all three platforms carry the rule (no "Xvfb" in those lines).
2. **Z82M staging.** New `docs/qa_evidence/2026-09-24-tryit-Z82M/unstage.sh`: kill `fake.pid`, `gateway.pid` and any `relay.pid`/`xvfb.pid` present, then a full-path sweep for the sandbox, wait, `rm -rf "$SANDBOX"`. Make `stage.sh` self-cleaning on rerun — before `rm -rf`, kill pids from any existing `$SANDBOX/*.pid` — and print its open line in the form `… & echo $! > "$SANDBOX/relay.pid"` so the app pid is recorded. Add a header comment naming #8YNJ so the landed evidence record stays honest.
3. **Tests.** `tests/test_tryit_protocol.py`: assert the Linux prompt contains `pkill -x relay` as prohibition plus the pid-file teardown instruction. Mirror one line in the macOS and Windows prompt tests. All must keep `assertNotIn('Xvfb', brief)` green.
4. **Protocol doc.** `docs/AGENT-SESSIONS-PROTOCOL.md` §31.10: add one sentence on the teardown rule if the section describes staging duties; skip if it defers wholly to the brief.

**Orchestration.** None — one small change-set, single agent; no subagents.

**Risks.**

- Editing landed evidence (`stage.sh` under `docs/qa_evidence/…Z82M/`): mitigated by the #8YNJ header comment; `expected.md` and screenshots untouched.
- The brief is deliberately short — keep the addition to a few lines or the Try-it prompt bloats.
- `_platform_brief()`'s span replacement is text-anchored (`For the app:` … `For a backend behaviour`); text placed outside those anchors changes what macOS/Windows see — the step-3 tests catch a mistake.
- No owner decision needed: the card already chose the mechanism (pid file / full path / private runtime dir).

**Verify.**

- `pytest tests/test_tryit_protocol.py tests/test_macos_tryit.py tests/test_tryit_windows.py -q` — targeted only, per the repo rule.
- Live: run `stage.sh` twice back-to-back (the second must succeed with no manual killing), launch the printed open line, run `unstage.sh`, then check `pgrep -af tryit/z82m` is empty, ports 4761/4762 refuse connections, and the live app's pid (`pgrep -x relay` before and after) is unchanged.
- `rg -n 'pkill -x relay'` across the repo matches only the prohibition text and the incident cards.
