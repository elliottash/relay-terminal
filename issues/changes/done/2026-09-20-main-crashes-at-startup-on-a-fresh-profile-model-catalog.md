---
id: 561P
type: work
status: done
labels: [bug, models, gui]
component: [gui]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
verified_by: openai/gpt-6-astra via codex
priority: 3
rank: zzzzzzzzzzzzzzzz
created: '2026-09-20'
source: 'Claude Code session on #7BM4, 2026-09-20, while building a clean export of main for human QA'
links: {plans: [], commits: [4fbf48b137b1d1978fe99e045c428d919d9ae8d2], evidence: [docs/qa_evidence/2026-09-20-switchboard-tooling-hub/startup-crash-a2204ba4.log, docs/qa_evidence/2026-09-21-561P-current/, docs/qa_evidence/2026-09-21-verify-561P-a1/], related: [7BM4], github: null}
---
# main crashes at startup on a fresh profile: heap corruption in the model catalog parser

## Issue
A clean export of `main` at `a2204ba4` (`git archive main | tar -x`, `cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo`, its own `backend/`), started with `--clean-shell --fresh` on an isolated profile (`RELAY_KEYRING=off`, fresh XDG dirs, no keys), aborts within a second of launch:

```
malloc(): unaligned fastbin chunk detected
gui_crash signal=6 name=SIGABRT … uptime_s=0
relay::models::catalogFrom(QJsonArray const&)+0xd7c   → src/ModelCatalog.cpp:162
Pane::modelCatalog() → Pane::currentEntryKey() → Pane::refreshPickers() → Pane::changed()
→ Pane::configurePreset(QString const&, bool, QString const&) → Pane::handle(QJsonObject const&)
```

The same launch of a clean export at `f72960e2` (earlier the same day) starts and runs. Between the two, the only commit touching `src/ModelCatalog.*` is `c2110e8f` ("an open-ended provider's long tail is hidden until checked"), which adds `Entry::openEnded` and `inAnyList()`; `src/Pane.h` changed in the same commit. Line 162 is `entry.openrouter = str(row, "openrouter")`, where malloc detects a heap already corrupted, so the write that corrupts it is earlier in that loop or in what `configurePreset` did before it.

Reproduce (no display needed beyond Xvfb):

```
X=$(mktemp -d /tmp/claude-1000/x.XXXX) && git archive main | tar -x -C $X && cmake -S $X -B $X/build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build $X/build -j16 --target relay
HOME=$(mktemp -d) RELAY_KEYRING=off xvfb-run -a $X/build/relay --workspace $PWD --clean-shell --fresh
```

It blocks human QA of anything on a fresh profile (#7BM4's walkthrough is waiting on it). A running Relay built before `c2110e8f` is unaffected until it is rebuilt.

## Done means
- A clean committed-tree build starts on an isolated empty profile with its matching backend, reaches worker-ready/model selection and stays alive until deliberately stopped.
- Capture revision, binary identity, isolated launch settings and logs; distinguish a current non-reproduction from a proven explanation of the historical heap corruption.
- Failure is SIGABRT/SIGSEGV, allocator corruption, or exit before the deliberate stop.

## Execution Summary
The original crash does not reproduce on clean committed tree 82acbc04993a with its matching backend and an isolated empty profile. It reached worker-ready/model configuration and stayed alive until a deliberate 15-second timeout. No model-catalog code change was warranted. The historical heap-corruption cause remains unestablished; see docs/qa_evidence/2026-09-21-561P-current/README.md.
Session handoff, 2026-09-21: remains Done. Parent and independent verifier reproduced clean empty-profile startup successfully at 82acbc04 with matching backend; independent evidence 4fbf48b1, final record 9abd3984. docs/qa_evidence/2026-09-21-verify-561P-a1/report.md identifies binary, manifest and deliberate stop. This is current non-reproduction, not a diagnosed fix for historical heap corruption. No further work unless the crash reproduces.

## Tests
manual: docs/qa_evidence/2026-09-21-561P-current/README.md

## QA checklist
Independent Codex verifier a1, 2026-09-21 local / 2026-09-22 UTC.
- Done means 1 — PASS: own empty-profile Xvfb launch reached worker-ready and rendered Models; alive at 15 seconds and at deliberate stop after 20.08 seconds. GUI exit 0; worker exit 0/crashed 0.
- Done means 2 — PASS: exact GUI/backend revision 82acbc04993af406b9b091f659165e6ba356241c; 389 relevant gate manifest entries matched. Binary SHA256, isolation settings, log and screenshot recorded in docs/qa_evidence/2026-09-21-verify-561P-a1/.
- Failure condition — absent: no SIGABRT/SIGSEGV, allocator corruption diagnostic or early exit; gui_quit reason=signal reflects deliberate SIGTERM.
- Tests manual README reproduction — PASS by fresh independent run.py execution, with all HOME/XDG/runtime/tmp directories initially empty and matching clean backend.
Evidence: docs/qa_evidence/2026-09-21-verify-561P-a1/report.md; commit 4fbf48b137b1d1978fe99e045c428d919d9ae8d2.

## Verdict
2026-09-21 local / 2026-09-22 UTC — **PASS for current non-reproduction**, independently repeated on clean committed build 82acbc04 with matching backend. No implementation fix was invented. The historical heap-corruption cause at a2204ba4 remains unknown; a single successful startup is not proof against intermittent corruption. Parent may resolve as no longer reproduced under the recorded conditions; this is not a diagnosed memory-safety fix.
