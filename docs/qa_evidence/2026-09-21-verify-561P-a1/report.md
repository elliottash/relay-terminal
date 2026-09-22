# Independent #561P verification

Review: 2026-09-21 local / 2026-09-22 UTC, Codex verifier a1, independently launched from the implementer's run.

**PASS for the card's current non-reproduction criteria. Historical root cause remains unknown.**

| Criterion | Verdict | Evidence |
|---|---|---|
| Clean committed-tree build, isolated empty profile, matching backend, reaches worker/model readiness, remains alive to deliberate stop | PASS | `result.json`, `startup.log`, visually inspected `ui.png`: worker-ready in 0.17 seconds, model configuration processed, Models page populated; alive at 15 seconds and immediately before deliberate SIGTERM at 20.08 seconds. GUI returns 0; worker exits code 0/crashed 0. |
| Revision, binary identity, isolation settings and logs; distinguish non-reproduction from root-cause proof | PASS | Exact revision and SHA256 below and in `result.json`; no memory-safety fix asserted. |
| Failure means SIGABRT/SIGSEGV, allocator corruption or early exit | PASS: absent in this run | No `gui_crash`, allocator diagnostic or early exit; `gui_quit reason=signal` is the deliberate stop. |
| Tests: `manual: docs/qa_evidence/2026-09-21-561P-current/README.md` | PASS by independent repetition | `python3 docs/qa_evidence/2026-09-21-verify-561P-a1/run.py`; fresh sandbox `/tmp/v561p-5jlyqmvn`, Xvfb `:300`, logs/screenshot captured. |

GUI `/tmp/claude-1000/land/1cxd-b/verify/build/relay`, SHA256 `c285932e3cf5dbb925c9f1b78b53a53e5b7af737779296f6f3ab14e9ecc00ca9`; matching data `/tmp/claude-1000/land/1cxd-b/verify/src`. Compared 389 build-gate manifest entries under src/, engine/, backend/, shell/ and CMakeLists.txt against `82acbc04993af406b9b091f659165e6ba356241c`: zero mismatches. No shared working-tree backend was used.

Empty HOME/config/data/cache/runtime/tmp were created just before launch; no profile file was seeded. Keyring disabled, `RELAY_NO_ISOLATION=1` matching the implementer's reproduction, explicit matching data root. No model prompt, credentials or model call. The screenshot shows guest providers not logged in. App and Xvfb were stopped after capture. This is one startup observation, not a stress run or proof against intermittent memory corruption.

No implementation change. The original failure at a2204ba4 was not reproduced or diagnosed here; the run establishes only that the recorded 82acbc04 build no longer exhibits that immediate failure under the tested conditions. Parent can resolve the card as current non-reproduction with that limitation explicit. No real Human QA decision was changed.
