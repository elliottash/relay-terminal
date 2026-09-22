---
id: SPB2
type: work
status: needs-verification
labels: [feature, build]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: mspb2
created: '2026-09-22'
source: Codex in Relay, 2026-09-22
links: {plans: [], commits: [f589f8c57bb5efdefa4b4bdbceb6bf064b874ebf], evidence: [docs/qa_evidence/2026-09-22-sphinxpad-build/], related: [SBT2, 3BPH], github: null}
---
# Fresh integrated build for sphinxpad

## Issue
merge in everything, make a fresh build, and upload it to sphinxpad

## Done means
- All available committed work is included in the source revision; any exclusions are explained.
- A fresh Ubuntu 26.04 amd64 package is built and verified.
- The package is available on sphinxpad with its revision and checksum recorded.

## Plan
Build and deliver main using the native Debian packaging script in an isolated source export on sphinxpad. Fetch and inspect refs first, then transfer a fixed revision, build, verify, and record the artifact path. Preserve shared checkout work. Verify package metadata and executable startup without replacing the running installation.

## Execution Summary
All committed work already on main; fetched origin, no incoming commits or side branches. Built clean fff7eb8fdf4617e2cc845805c209f32e264dd77d natively on sphinxpad (Ubuntu 26.04 amd64, Qt6, pinned Ghostty, system Python 3.14). Package: sphinxpad:/home/elliott/relay-debs/relay_0.1.0~gitfff7eb8f-1~ubuntu26.04_amd64.deb. Extracted-package startup/worker/shell smoke PASSED. Full test gate FAILED: boardworkspace, boardexecute, backend timeout and two engine cases; tracked in #SBT2 and #3BPH. Artifact packaged with CPack after recording failed gate, and is a development build, not a verified release. Existing installed Relay was not replaced. Evidence: docs/qa_evidence/2026-09-22-sphinxpad-build/.

## Tests
manual: docs/qa_evidence/2026-09-22-sphinxpad-build/README.md
manual: docs/qa_evidence/2026-09-22-sphinxpad-build/smoke.log
manual: docs/qa_evidence/2026-09-22-sphinxpad-build/build-system-python.log
manual: docs/qa_evidence/2026-09-22-sphinxpad-build/engine-test.log
