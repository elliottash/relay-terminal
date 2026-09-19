---
id: ZQM3
type: work
status: needs-qa-llm
labels: [ui, composer, pane-chrome, sharing]
implemented_by: warp-oz/glm-5.3-flash
rank: zzzzzzzy
created: '2026-09-19'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-model-box-flat-and-share-in-chrome/], related: [], github: null}
---
# the model box is one flat list that hugs its text; the share button moves to the pane's chrome row

## Issue
Owner, 2026-09-19: *"2 requests. shrink the model selector to the text length of the selected
model. when uncollapsed the list can get wider. dont have a separate main / flash / local section,
just put those in parentheses. the order should be main / flash / local / others."* — and on the
share button: *"it doesnt fit there with the agent options. should we put it in the top right
corner, just below the close pane x?"* (it went into the chrome row's own button row, which
already reserves the header's right corner).

## QA checklist
Model box (`Pane::refreshPickers`, `Pane::remoteState`, new `CurrentTextComboBox` in `src/Pane.h`):

- [ ] `ctest -R panestate` passes: the pane_state menu's `model.choices` mirror the box's order
      and labels (`role:` rows first as "model (role)", then `preset:` rows).
- [ ] Collapsed chip hugs the current row's text ("glm-5.3 (main)"), and widens only when a wider
      row is picked; the open list grows to its widest row instead of the widest always.
- [ ] Open list order: `model (main)`, `model (flash)`, `model (local)` when a local endpoint is
      served, then the other presets — no section separator before them, no ticks, and the
      "No stored keys" row is gone; guests and the ⚙ gear stay behind their separators.
- [ ] Picking a role row swaps the agent role (Alt+F's path), a preset row switches the model, the
      ⚙ row opens the model-options modal — all still, after the reorder.

Share button (`src/PaneChrome.h` `buildShare`, `src/Theme.cpp`):

- [ ] The share chip is gone from the composer strip; a share button sits in the chrome row left
      of the split buttons, with the share icon (↗ fallback) and tooltip "Share this pane with a
      phone…", and clicking it opens the share dialog exactly as the chip did.
- [ ] While the pane is shared the button wears the agent violet (`dest="agent"`) and its tooltip
      says who is there; the tooltip updates live while a share state changes.
- [ ] Tool-pane chrome rows (explorer, Switchboard, …) are unchanged — the share button is
      terminal panes only.

`scripts/relay-build` clean; `ctest --test-dir build` green apart from the pre-existing
`buttonfit` failure (#916B's committed `tabProjectChip` 8.5pt rule; not this change);
`./scripts/test.sh` fully green (3413 tests, 2026-09-19).

Live verification under Xvfb is **pending QA**: run
`docs/qa_evidence/2026-09-19-model-box-flat-and-share-in-chrome/drive.sh`, which drives the
collapsed chip, the flat list, the role swap and the share button (tooltip, dialog, violet state)
and writes implementer-*.png next to itself. Only the probe frame is in the evidence directory so
far.

Evidence: docs/qa_evidence/2026-09-19-model-box-flat-and-share-in-chrome/
