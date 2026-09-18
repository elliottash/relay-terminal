# Evidence — file tools accept absolute paths and `..`, still confined (#E99H)

Change: `Workspace.resolve` in `backend/relay_core/tools.py` no longer rejects absolute
paths and `..` up front. Every path is resolved and must land inside the workspace
(`Path escapes the workspace` otherwise); the symlink refusal and the secret-file guard
now walk the resolved, workspace-relative path, so they behave identically for absolute
input. Diff: 23 lines in tools.py, 14-line regression test in tests/test_tools.py.

## What was run (2026-09-19, implementer)

- `PYTHONPATH=backend python3 -m unittest tests.test_tools tests.test_agent
  tests.test_skills tests.test_program_input -v` → `Ran 88 tests … OK`
- `./scripts/test.sh` → `Ran 943 tests in 78.429s, FAILED (failures=4, errors=6)`; all 10
  are pre-existing failures in tests.test_remote_host / test_remote_wire /
  test_remote_noise, identical with this change stashed (tracked in #XEMH).

## New behaviour pinned by tests

- `test_absolute_and_parent_paths_allowed_inside_workspace`: reads via absolute path and
  `sub/../file.txt` succeed; `../`, `/`, the workspace parent and `sub/../../` raise.
- `test_path_escape_and_secret_guard` (unchanged): `../etc/passwd`, `/etc/passwd`,
  `.env*`, `.ssh/id_rsa`, `.git/config`, `secret.pem` and a symlink out still refused.
- `test_symlink_swap_after_approval` (unchanged): symlink swapped in after prepare is
  caught at execution time.

## QA checklist pointer

GUI is not affected (worker-only change); no screenshots. A QA session should re-run the
two tool tests and try one absolute path and one `..` path inside a workspace through a
live worker.
