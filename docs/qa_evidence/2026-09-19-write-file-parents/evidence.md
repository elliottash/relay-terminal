# write_file creates missing parent directories — implementer evidence (card #NC17)

2026-09-19, relay-agent pane (this session).

## What was wrong

`write_file` refused any path whose parent directory did not exist: "Parent directory must
already exist. Relay does not create directory trees automatically." The model then spent a full
round trip on `run_command mkdir` and retried the write — observed in session
30873d1d932a4e21a3cfff37d305af37 (`write drive.sh ✗` → `ran mkdir` → `wrote drive.sh`).
The refusal was a guard against typo'd paths growing stray directory trees, but a write is
already previewed, approved, confined to the workspace (locally) or the remote home/cwd (over
ssh), checkpointed and rewindable, so the guard cost more than it protected.

## What changed

- `backend/relay_core/tools.py`
  - prepare: the parent-must-exist refusal for `write_file` is gone.
  - execute: `path.parent.mkdir(parents=True, exist_ok=True)` before the atomic temp-file
    replace; an `OSError` there (e.g. a file in the way) becomes a `ValueError` the model can
    act on, not a traceback.
- `backend/relay_core/remote_files.py` (the same rule on the ssh host, card #S5SH's scripts)
  - `write_script`: `mkdir -p -- "$d"` instead of exiting 68 (`NO_PARENT`).
  - `read_script(optional=True)`: a missing parent is simply a missing file (exit 66) — the
    before-picture of a write that is about to create the tree.
  - `NO_PARENT` (exit 68) removed; nothing emits it any more. `mkdir` added to the documented
    command list in the module docstring and in the host-missing-a-command error.
- `docs/AGENT-SESSIONS-PROTOCOL.md`: the v2.3 write paragraph says write_file makes the parent
  directories its path needs.
- Tests
  - `tests/test_tools.py::test_write_file_makes_missing_parent_directories` (new): deep write
    creates the tree; a file in the way is an actionable ValueError.
  - `tests/test_ssh_remote.py::test_a_new_file_is_private_and_its_parents_are_made` (rewritten):
    the old test asserted the refusal; it now asserts a deep remote write succeeds. The fake-ssh
    harness runs the scripts for real, so `mkdir -p` is exercised.

## Verification

```
XDG_DATA_HOME=$(mktemp -d) RELAY_KEYRING=off PYTHONPATH=$PWD/backend \
  python3 -m unittest tests.test_tools tests.test_ssh_remote -v
→ Ran 79 tests in 3.6s — OK
```

Both new tests pass; the whole of both files' suites (79 tests) pass.

## Notes for QA

- `edit_file` is untouched in behavior: it still requires an existing file, and its parent
  therefore exists (the mkdir in execute is a no-op for it).
- Rewind after a write that created directories removes the file but leaves the now-empty
  directories — the same as rewinding after a `mkdir` run_command. Deliberate.
- Nothing in C++ or in the model-facing prompts named the old refusal; repo-wide grep for
  "directory trees" / "Parent directory" comes back empty outside the issue tracker.
