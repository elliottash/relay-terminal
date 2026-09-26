<!-- relay:entry 20260925T175838Z-bw author=agent kind=event model=glm-5.3 pane=bdee80e5 turn=9a7210193ea2463396bd027ff72b34f1/7b4a912214584b4a9df53118051be27c -->
- ✦ agent created this card in Planning · .board/features/2026-09-25-flexible-pane-memory-caps-with-process-level-oom.md

<!-- relay:entry 20260925T175912Z-a1 author=agent kind=event model=glm-5.3 pane=bdee80e5 turn=9a7210193ea2463396bd027ff72b34f1/7b4a912214584b4a9df53118051be27c -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "system", "primary": "probe", "also": ["script"], "human": "none", …; appended to `## Decisions`

<!-- relay:entry 20260925T175921Z-gv author=agent kind=event model=glm-5.3 pane=bdee80e5 turn=9a7210193ea2463396bd027ff72b34f1/7b4a912214584b4a9df53118051be27c -->
- ✦ agent updated this card · appended to `## Done means`

<!-- relay:entry 20260925T175934Z-6r author=agent kind=event model=glm-5.3 pane=bdee80e5 turn=9a7210193ea2463396bd027ff72b34f1/7b4a912214584b4a9df53118051be27c -->
- ✦ agent updated this card · appended to `## Plan`

<!-- relay:entry 20260925T175938Z-60 author=agent kind=event model=glm-5.3 pane=bdee80e5 turn=9a7210193ea2463396bd027ff72b34f1/7b4a912214584b4a9df53118051be27c -->
- ✦ agent updated this card · appended to `## Tasks`

<!-- relay:entry 20260925T175945Z-bq author=agent kind=event model=glm-5.3 pane=bdee80e5 turn=9a7210193ea2463396bd027ff72b34f1/7b4a912214584b4a9df53118051be27c -->
- ✦ agent updated this card · appended to `## Planning notes`

<!-- relay:entry 20260925T175957Z-32 author=agent kind=decision model=glm-5.3 pane=bdee80e5 turn=9a7210193ea2463396bd027ff72b34f1/7b4a912214584b4a9df53118051be27c -->
Owner approved the design in this pane's session: generous per-pane caps under a shared app-relay.slice ceiling sized to the machine, with process-level OOM kills (OOMPolicy=continue + oom_score_adj 1000 on tool children). Quoted: "i agree, put a plan on the card" (after "i agree, implement this plan"). Scope split agreed: the per-run run_command memory_max override lives on #WBDX.

<!-- relay:entry 20260925T181117Z-t3 author=agent kind=event model=glm-5.3 pane=bdee80e5 turn=9a7210193ea2463396bd027ff72b34f1/d1d5756941ff41018e5447158412546a -->
- ✦ agent claimed this card · assignee agent, Planning → Executing, implemented_by glm/glm-5.3, session bdee80e5

<!-- relay:entry 20260925T181117Z-t4 author=agent kind=progress model=glm-5.3 pane=bdee80e5 turn=9a7210193ea2463396bd027ff72b34f1/d1d5756941ff41018e5447158412546a pane_token=bdee80e5-afec-4716-ac57-b9f6bf76e173 -->
Claimed (bdee80e5) · working on it from a terminal pane

Implementing steps 0-5 of the approved plan: process-level agent OOM kills, app-relay.slice ceiling, generous defaults, docs and tests.

<!-- relay:entry 20260925T181629Z-vc author=agent kind=evidence model=glm-5.3 pane=bdee80e5 turn=9a7210193ea2463396bd027ff72b34f1/d1d5756941ff41018e5447158412546a -->
Step 0 gate PASSED on this machine (systemd 255, cgroup v2), probes in `~/.cache/relay/scratch/9a7210193ea2/Step-0-gate-probes-for-card-ZPWT-slice-n-scaa08f/`:

1. **Slice nesting + binding**: `systemctl --user set-property --runtime app-relay-probe.slice MemoryMax=1500M MemorySwapMax=0` created the slice (runtime drop-ins under `/run/user/1000/systemd/user.control/…`, nothing persistent; `systemctl revert` removes them cleanly). A scope started `--slice=app-relay-probe.slice` (no own MemoryMax) landed at `…/app.slice/app-relay.slice/app-relay-probe.slice/<scope>.scope`, and two hogs totalling past 1500M were OOM-killed by the **slice's** limit.
2. **Single-process kill**: scope ran `-p OOMPolicy=continue`; staggered hogs (850M at `oom_score_adj` 500 resident first, then 850M at 1000) → `RESULT adj500_exit=0 adj1000_exit=137 oom_kill=1`: exactly one process died, parent and 500-adj sibling survived, scope finished normally. (A simultaneous-growth run killed both — second kill raced the first victim's reclaim; noted as expected under racing allocators, the staggered case is the real one.)
3. **oom_score_adj precedence**: the 1000-adj hog was the kernel's pick over the 500-adj one in both runs.
4. **Own limit inside the slice** (#WBDX): a scope with `-p MemoryMax=300M` inside the 1500M slice ran a 250M hog fine — a child scope's own cap coexists with slice membership.
5. **Bonus finding**: `systemd-run --user --scope` works from the agent worker's children even with `DBUS_SESSION_BUS_ADDRESS` unset (sd-bus falls back to `$XDG_RUNTIME_DIR/bus`); my own cgroup is `relay-pane-bdee80e5-agent-1.scope`. The `Pane.h` comment "agent-run systemd-run --user is not supported anyway" is wrong on this machine — which makes #WBDX implementable directly in `jobs.py` with no D-Bus handed to the pane. Probe slice reverted and stopped.

<!-- relay:entry 20260925T183503Z-3h author=agent kind=event model=glm-5.3 pane=bdee80e5 turn=9a7210193ea2463396bd027ff72b34f1/d1d5756941ff41018e5447158412546a -->
- ✦ agent updated this card · appended to `## Execution Summary`; tasks: 7/7 done

<!-- relay:entry 20260925T183509Z-50 author=agent kind=event model=glm-5.3 pane=bdee80e5 turn=9a7210193ea2463396bd027ff72b34f1/d1d5756941ff41018e5447158412546a -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260925T183515Z-53 author=agent kind=event model=glm-5.3 pane=bdee80e5 turn=9a7210193ea2463396bd027ff72b34f1/d1d5756941ff41018e5447158412546a -->
- ✦ agent updated this card · tasks: 7/7 done

<!-- relay:entry 20260925T183519Z-kg author=agent kind=event model=glm-5.3 pane=bdee80e5 turn=9a7210193ea2463396bd027ff72b34f1/d1d5756941ff41018e5447158412546a -->
- ✦ agent moved this card · Executing → Needs verification · Implemented in 0bfe33ee + f2e7fa69: two-layer caps (app-relay.slice ceiling, RAM-scaled per-pane caps), process-level OOM kills, memory_max per-run override; targeted suites green, evidence bundle landed. · evidence docs/qa_evidence/2026-09-25-zpwt-pane-memory-caps/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260925T183543Z-p0 author=agent kind=progress model=glm-5.3 pane=bdee80e5 turn=9a7210193ea2463396bd027ff72b34f1/d1d5756941ff41018e5447158412546a -->
- ✦ agent is preparing Try it for this card

<!-- relay:entry 20260925T184309Z-pe author=agent kind=event model=glm-5.3 pane=bdee80e5 turn=9a7210193ea2463396bd027ff72b34f1/d1d5756941ff41018e5447158412546a -->
- ✦ agent updated this card · appended to `## Try it`

<!-- relay:entry 20260925T184317Z-24 author=agent kind=evidence model=glm-5.3 pane=bdee80e5 turn=9a7210193ea2463396bd027ff72b34f1/d1d5756941ff41018e5447158412546a -->
Try-it mechanical pass, run while staging (full capture `docs/qa_evidence/2026-09-25-tryit-ZPWT/01-tryit-run.txt`):

```
=== A. plain run_command, hog past the pane cap (pane scope: 1500M)
 "exit_code": -9, "killed_for_memory": true,
 "note": "exit code -9 (SIGKILL): the kernel killed this command for memory — it passed
          the pane's cap. Run it again with run_command memory_max (a size like 64G)
          for a bound that fits."
=== B. run_command memory_max=600M, hog 900M past its own bound
 "exit_code": -9, "killed_for_memory": true,
 "note": "exit code -9 (SIGKILL): this command ran in its own scope with memory_max=600M,
          and was killed for passing that bound."
PANE-ALIVE: this pane process survived both kills and is still reporting.
```

Staging caught one real defect, fixed in 342c7280: without `MemorySwapMax=0`, a job swaps past its own bound (a 900M hog survived a 600M memory.max — memory.max caps RAM, swap needed its own cap). Per-run scopes now pin swap to 0, and a live enforcement test covers it (35 passed after the fix).

<!-- relay:entry 20260926T011916Z-fh author=agent kind=event mention=3MH4 model=claude-opus-5-5 pane=switchboard turn=fe996bec0475c2d900e5f8108ff94bf7/965237f0039544adb00b264bb94a443c -->
mentioned in #3MH4 · 2026-09-26 · agent
