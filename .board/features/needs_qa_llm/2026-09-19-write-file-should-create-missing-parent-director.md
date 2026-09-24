---
id: NC17
type: work
status: needs-qa-llm
labels: [feature, agent-tools, files]
implemented_by: glm/glm-5.3
rank: zzzzzzzz
created: '2026-09-19'
source: pane report from session 30873d1d932a4e21a3cfff37d305af37
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-write-file-parents/], related: [], github: null}
---
# write_file should create missing parent directories instead of refusing

## Issue
potential bug or feature adjusmtnet needed"

▸ ✦ thought for 79 s
--workspace and --fresh it is. Now the evidence drive script:

▸ write drive.sh ✗ · Parent directory must already exist. Relay does not create directory trees automatically.
▸ ran mkdir · exit 0
▸ wrote drive.sh · new · 136 lines

▸ ✦ thought for 55 s

▸ wrote orient.py · new · 61 lines
▸ ran chmod +2 · 1 line · exit 0
▸ ran drive.sh +2 ✗ · exit -15 · 95 s

▸ ✦ thought for 12 s
The drive run was killed after ~95 s. Let me check what it left behind:

from session 30873d1d932a4e21a3cfff37d305af37

## QA checklist
- [ ] Local: `write_file` to `deep/deeper/new.txt` in a fresh workspace creates the tree and the file (test_write_file_makes_missing_parent_directories).
- [ ] Local: a regular file in the way of the directories fails with an actionable ValueError, nothing written.
- [ ] Remote: `write_file` with `host` to a nested missing path succeeds; the fake-ssh suite runs the scripts for real (test_a_new_file_is_private_and_its_parents_are_made).
- [ ] `edit_file` behavior unchanged: still refuses a file that does not exist.
- [ ] Full `tests.test_tools` + `tests.test_ssh_remote` pass (79 tests at implementation).
- [ ] No stray references to the old refusal (grep "directory trees", "Parent directory", NO_PARENT).
