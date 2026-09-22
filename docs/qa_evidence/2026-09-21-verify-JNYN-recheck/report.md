# Independent #JNYN repair recheck

2026-09-21 local / 2026-09-22 UTC — Codex verifier a1, independent of implementer.
**PASS for both reported defects and the real UI answer flow.** This supersedes the two FAIL findings in the earlier independent report; it does not close the broader end-to-end staging criteria.

- Fresh rerun of the original verifier reproduction (`python3 docs/qa_evidence/2026-09-21-verify-JNYN-fresh/drive.py backend`): successful retry now emits `outcome=done`, `section_written=true`, `staging_failed=false` and the successful report; the previous failure note is ignored. Evidence: `backend.txt`.
- The same reproduction now preserves `Answer: Keep the old layout.` and adds generated Try it content inside a dedicated marker block. Evidence: `backend.txt`.
- `PYTHONPATH=/tmp/jnyn-fixed-source/backend python3 -m unittest discover -s tests -p test_tryit_protocol.py -v`: 34/34 PASS, `tests.txt`.
- Fresh isolated Xvfb actual UI: opened Board with Ctrl+Shift+S, expanded Needs verification, opened disposable #T9QA, clicked Open it (executed `printf JNYN-FRESH-OPEN`), typed `Independent recheck: clear.` in the answer field and pressed Enter. Expected result appeared only afterward. Existing unrelated Human QA answer survived in the real file alongside the generated block. Evidence: `ui.png`, `fixture.md`.

## Exact runtime

GUI binary: `/tmp/claude-1000/land/1cxd-b/verify/build/relay`, SHA256 `c285932e3cf5dbb925c9f1b78b53a53e5b7af737779296f6f3ab14e9ecc00ca9`. Independently compared every `src/`, `engine/`, and `CMakeLists.txt` blob in its build-gate manifest against commit `82acbc04`: zero mismatches. This identifies the C++ revision rather than claiming the GUI was rebuilt for the backend-only fix.

Backend/data: clean `git archive 1f3a7af0 backend shell scripts` exported to `/tmp/jnyn-fixed-source`, selected with `RELAY_DATA_DIR`. Live worker command lines confirmed `/tmp/jnyn-fixed-source/backend/worker.py`. Both repaired backend paths match commit `1f3a7af0e7467dff0037cd56d285e3aef2d19727`. Original verifier reproduction used unchanged working-tree backend files at that revision; unit tests and UI used the clean export.

Fixture `/tmp/jnyfresh-_fa3fshu`, display `:283`; separate HOME, config, runtime, data, cache and tmp; keyring off. `launch.py` records the launch recipe. Fixture was created with the prior verifier's `drive.py stage`, then seeded with an unrelated Human QA answer before launch. All answers in these artifacts are synthetic verifier input, not owner decisions. Test processes were stopped afterward.

## Scope remaining

Done means 3's stale-failure issue and Done means 4's decision-loss issue are cleared. Done means 1, 2 and 5 still require the broader configured-model staging evidence; another independent agent is driving #7BM4, per parent coordination. No additional actual failure observed in this narrow recheck. No implementation, real Human QA, or parent-owned card edits. Parent may incorporate this independent verdict into the card after combining the end-to-end evidence. `relay_board` was not visible; no delegation used.
