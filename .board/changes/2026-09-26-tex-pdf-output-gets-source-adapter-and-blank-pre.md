---
id: SJ00
type: work
status: executing
labels: [bug, files, plugins]
assignee: agent
implemented_by: openai/gpt-6-sol via codex:ashe-ethz-ch
session: dc713c52-45bc-4954-ba65-72ecb17516de
discovered_from: C0Q8
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-26'
verify: {artifact: code, primary: script, also: [], human: optional, criteria: The PDF output is read only and the status explains a build without Qt PDF., sign_off: none, effort: low}
links: {plans: [], commits: [9fe227602f99], evidence: [], related: [E85D, R660, WYGY], github: null}
---
# TeX PDF output gets source adapter and blank preview

## Issue
A TeX chain registers `main.pdf` using its source file adapter in `src/RelayWindowWorkspace.cpp`, so the output is marked editable text. On a build without Qt PDF support, the PDF preview is blank without an explanation.

## Done means
A TeX chain registers its PDF output with the PDF preview adapter and does not mark it editable. When Qt PDF support is absent, the preview states why and offers the existing external-open path.

## Execution Summary
The TeX chain registers `main.pdf` with the adapter resolved from its output path. That gives PDF generated/read-only authority and lets the existing status strip explain missing Qt PDF support. Commit `9fe22760`, queue job `c8923408f46286f6` (publication pending).

## Tests
- ctest -R artifactworkspace
- ctest -R filepanes
- Manual run: `scripts/relay-build --target relay` succeeded; both focused CTest cases passed.
