# Shift-click external file opening — implementer evidence

Card: #SFC1

The reported file exists at
`/home/elliott/admin/Advisees/joe-lamb/Dissertation-Report-Lamb-Ash.docx`.

## Automated checks

- `ctest --test-dir build --output-on-failure -R '^(relay-engine-tests|consolemode)$'`
  passed: 2/2 tests. The engine test performs a real Shift-click on an existing
  `report.docx` link and checks that `Qt::ShiftModifier` reaches the activation signal. The
  console test checks that the same modifier routes the real path to the external opener and
  bypasses the context and Relay preview.
- `RELAY_SESSION=codex-shift-click scripts/relay-build --target relay` passed.
- `RELAY_SESSION=codex-shift-click scripts/relay-build --target relay-engine-tests relay-consolemode-tests`
  passed.

## Manual verification for the verifier

In a terminal pane that displays the reported path as a clickable link, hold Shift and click it.
The system DOCX application should open the file; Relay should not open a preview pane.
