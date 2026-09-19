# Tracker fixtures

One small, hand-written project per tracker format `relay_core.project_probe` reads, in the
shape that format's own documentation specifies (the URLs are in the parser docstrings in
`backend/relay_core/project_probe.py`). They are inputs to `tests/test_project_probe.py` and
`tests/test_board_import.py` and are never written to: a test that needs to modify a tree
copies it first, and `test_board_import` asserts these files are byte-identical after an
import.

There is deliberately no git fixture here — a `.git/` directory cannot be committed inside a
git repository, so the tests build `.git/config`, `.git/HEAD` and the `gitdir:` pointer files
themselves in a temporary directory.
