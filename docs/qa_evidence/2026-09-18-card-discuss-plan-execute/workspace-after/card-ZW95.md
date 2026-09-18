---
id: ZW95
type: work
status: needs-qa-llm
labels: [bug]
assignee: agent
implemented_by: glm-5.3
rank: zzzz105
created: '2026-09-18'
acceptance: completing a folder inserts the path and nothing else
source: 'issues/bug_intake.txt, 2026-09-18: "when i did tab autocompelte and selected a folder, it added a "-" for some reason: "cd 2026-09-18-EG/-" and then the command failed."'
links: {plans: [], commits: [4b2063a], evidence: [docs/qa_evidence/2026-09-18-tab-completion-stray-dash/], related: [], github: null}
---
# Tab completion adds a stray "-" after a folder

## Request
when i did tab autocompelte and selected a folder, it added a "-" for some reason: "cd 2026-09-18-EG/-" and then the command failed.

## Plan
## Plan

### Goal
Completing a folder must insert the path and nothing else: choosing `2026-09-18-EG` on `cd 2026-09-18-…` yields `cd 2026-09-18-EG/`, never a trailing `-` (card acceptance).

### Findings
- `src/complete.py` is the whole completion feature (only file in `src/`).
- `candidates()` line: `out.append(name + ("/-" if os.path.isdir(full) else ""))` — folders get `"/-"` instead of the `"/"` its own docstring promises ("folders get a trailing separator"). The stray `-` comes from this literal.
- `complete()` itself is fine: it swaps the last word for the chosen candidate, so fixing the suffix in `candidates()` fixes the reported `cd 2026-09-18-EG/-`.
- No tests exist anywhere in the repo; no other file references completion.

### Steps
1. In `src/complete.py::candidates`, change the folder suffix `"/-"` to `"/"` so directories complete to `name + "/"`.
2. Add `tests/test_complete.py` (stdlib `unittest`, no new deps) covering: a directory candidate ends in `/` and contains no `-` suffix; a file candidate has no suffix; `complete("cd 2026-09-1", "2026-09-18-EG/") == "cd 2026-09-18-EG/".

### Risks
- Nothing else consumes the `-` marker (only `complete.py` mentions completion), so no caller can break; `complete()` is left untouched.
- No owner decision needed — the docstring already specifies the `/` separator.

### Verify
- `python -m unittest discover -s tests` (all green, including the new no-stray-dash assertions).
- Manual: in a tmpdir with folder `2026-09-18-EG`, `python -c "from src.complete import candidates; print(candidates('2026-09-18', '.'))"` prints `['2026-09-18-EG/']`.

## QA checklist
- [x] `python -m unittest discover -s tests` — 4 tests pass, including `test_folder_candidate_ends_with_separator_and_nothing_else` and `test_folder_candidate_has_no_stray_dash`.
- [x] Manual (plan Verify): in a tmpdir containing `2026-09-18-EG`, `candidates("2026-09-18", tmpdir)` returns exactly `['2026-09-18-EG/']`.
- [x] End-to-end: `complete("cd 2026-09-1", "2026-09-18-EG/")` returns exactly `cd 2026-09-18-EG/` — the path and nothing else (card acceptance).
- [x] `grep '"/-' src/` finds nothing; no other caller consumed the `-` marker (only `src/complete.py` mentions completion).
- [x] Evidence: `docs/qa_evidence/2026-09-18-tab-completion-stray-dash/verify.txt` (recorded test + manual output).
