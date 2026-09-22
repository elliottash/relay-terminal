# Named UI driving — #74Y5

Implementation: ece752ef33a41f3cb2fc1b3ee90a89c9c67863c5. Its exact committed candidate passed the land.py application build gate.

The implementer ran the rewritten `docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/ai-pass.sh` against a disposable staged `/tmp/relay-driven-orders` fixture under isolated Xvfb, HOME, XDG and TMPDIR. Each phase used a fresh application process. No input coordinates remain in the scenario.

Live binary: `/tmp/relay-driven-bin`, SHA256 `540e77566238789826d052806efe9b8505912b4a0f8654a14dd4aae006fe582e`. This was copied immediately after the successful shared-checkout build, including concurrent Hygiene work; these screenshots are not an exact-commit independent verification. The separate verifier checks the committed build.

- Phase a: named card open and rendered-section read; `boardTestsCheck` displays findings; `boardCardDone` refuses completion with the failing `totals_large_order` test and missing `test_invoice` reference. See a-read.json, a-findings.json, a-gate.json and a-00/a-11/a-13 images.
- Phase b: `action tests.open` reaches Test suites, showing seven tests and flaky `inventory_sync`; see b-panes.json and b-10-pane.png.
- Phase c: `boardProfile` then `profileTarget:build` completes the actual performance build and shows its eight steps and report.cpp.o hotspot; see c-10-menu.png and c-11-result.png.

Reproduction, after staging the disposable fixture with the adjacent scenario's stage.py:

```sh
RELAY_BIN=/path/to/relay bash docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/ai-pass.sh /path/to/disposable-orders /path/to/evidence a
RELAY_BIN=/path/to/relay bash docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/ai-pass.sh /path/to/disposable-orders /path/to/evidence b
RELAY_BIN=/path/to/relay bash docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/ai-pass.sh /path/to/disposable-orders /path/to/evidence c
```

The named-driver BoardPane regression passes. The combined Board suite exposed old cleanup-button assumptions after #SW1D; that card's implementer owns their repair. Human judgement and sealed expected results were not answered by this run.
