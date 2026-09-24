<!-- relay:entry 20260924T183106Z-ea author=agent kind=event model=glm-5.3 pane=0806c7fd turn=73326b0faa3643fe8938fe57f7045741/ff824ee9e69e49fbb7065fa488559843 -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-24-gui-echoes-app-catalog-to-every-worker-in-a-self.md

<!-- relay:entry 20260924T183118Z-ch author=agent kind=event model=glm-5.3 pane=0806c7fd turn=73326b0faa3643fe8938fe57f7045741/ff824ee9e69e49fbb7065fa488559843 -->
- ✦ agent updated this card · appended to `## Issue`

<!-- relay:entry 20260924T183126Z-fb author=agent kind=evidence model=glm-5.3 pane=0806c7fd turn=73326b0faa3643fe8938fe57f7045741/ff824ee9e69e49fbb7065fa488559843 -->
Reproduction/count commands (run on the live machine 2026-09-24 ~14:27 EDT):
- `grep -c app_catalog_updated ~/.local/share/relay/logs/relay.log` → 50185
- per-minute histogram of `app_catalog_updated` lines peaks 690/min at 18:18Z, 672/min at 18:19Z (14:18-14:19 EDT)
- main-thread CPU: two /proc/3747251 utime+stime deltas over 3-4 s → 30.0% then ~15% (tid == pid, GUI thread), app otherwise idle
- `tail` of relay.log around any `app_catalog_updated` batch shows the cycle: `event type=app_catalog_updated pane=…` × N, then `event type=presets pane=…`, then the next batch.

<!-- relay:entry 20260924T183126Z-fc author=agent kind=event model=glm-5.3 pane=0806c7fd turn=73326b0faa3643fe8938fe57f7045741/ff824ee9e69e49fbb7065fa488559843 -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260924T183627Z-2z author=agent kind=event model=glm-5.3 pane=0806c7fd turn=73326b0faa3643fe8938fe57f7045741/79ccab28f9384113b43169bbadcef032 -->
- ✦ agent claimed this card · assignee agent, Discussing → Executing, implemented_by glm/glm-5.3, verify defaulted from skill deliver, session 0806c7fd

<!-- relay:entry 20260924T183627Z-bk author=agent kind=progress model=glm-5.3 pane=0806c7fd turn=73326b0faa3643fe8938fe57f7045741/79ccab28f9384113b43169bbadcef032 pane_token=0806c7fd-6b22-4d1d-ac66-ec3e1ea8f5bb -->
Claimed (0806c7fd) · working on it from a terminal pane

Break the presets → notify → sendAppCatalog broadcast → echo loop by content-gating the catalog resend; add a test proving identical content sends nothing.

<!-- relay:entry 20260924T183904Z-zq author=agent kind=event model=glm-5.3 pane=0806c7fd turn=73326b0faa3643fe8938fe57f7045741/79ccab28f9384113b43169bbadcef032 -->
- ✦ agent updated this card · appended to `## Done means`

<!-- relay:entry 20260924T183911Z-q5 author=agent kind=event model=glm-5.3 pane=0806c7fd turn=73326b0faa3643fe8938fe57f7045741/79ccab28f9384113b43169bbadcef032 -->
- ✦ agent updated this card · appended to `## Plan`

<!-- relay:entry 20260924T185403Z-h7 author=agent kind=event model=glm-5.3 pane=0806c7fd turn=73326b0faa3643fe8938fe57f7045741/79ccab28f9384113b43169bbadcef032 -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260924T185408Z-n2 author=agent kind=event model=glm-5.3 pane=0806c7fd turn=73326b0faa3643fe8938fe57f7045741/79ccab28f9384113b43169bbadcef032 -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260924T185431Z-wc author=agent kind=event model=glm-5.3 pane=0806c7fd turn=73326b0faa3643fe8938fe57f7045741/79ccab28f9384113b43169bbadcef032 -->
- ✦ agent moved this card · Executing → Needs verification · Fix landed (fc6c5cce) with gate unit test green; per the board's verification ask it waits here for a verifying pass. · evidence docs/qa_evidence/2026-09-24-j0vy-n6y3-catalog-echo-and-math-gate/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260924T185438Z-z8 author=agent kind=progress model=glm-5.3 pane=0806c7fd turn=73326b0faa3643fe8938fe57f7045741/79ccab28f9384113b43169bbadcef032 -->
- ✦ agent is preparing Try it for this card

<!-- relay:entry 20260924T190114Z-tp author=agent kind=event model=glm-5.3 pane=0806c7fd turn=73326b0faa3643fe8938fe57f7045741/79ccab28f9384113b43169bbadcef032 -->
- ✦ agent updated this card · replaced `## Try it`

<!-- relay:entry 20260924T190119Z-bj author=agent kind=evidence model=glm-5.3 pane=0806c7fd turn=73326b0faa3643fe8938fe57f7045741/79ccab28f9384113b43169bbadcef032 -->
Try it staged by me (no verifying session had staged one): docs/qa_evidence/2026-09-24-tryit-J0VY/. Mechanical pass under Xvfb with the fixed binary — the staged script opened 8 panes, idled 60 s, and the sandbox relay.log held **0 `app_catalog_updated` events** (the pre-fix app logged a batch every few seconds; 50,185 in ~3.5 h on the live instance). What I saw:

![eight panes idle, catalog echo silent](docs/qa_evidence/2026-09-24-tryit-J0VY/01-eight-panes-idle.png)

staging-notes.md says how the fixture differs from real use (fresh profile, no keys, shell panes, disposable HOME under /tmp/claude-1000/tryit/j0vy).
