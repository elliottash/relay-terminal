---
id: DX4A
type: work
status: inbox
labels: [bug, tests, backend]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-25-verify-bug-cards], related: [], github: null}
---
# Python suites test_presets test_keybindings test_web_model_name fail at HEAD

## Issue
At clean HEAD `9a13cfe2` (git worktree export, so no uncommitted interference) the python suites that #4BPE named green on 2026-09-21 no longer pass:

`PYTHONPATH=backend python3 -m pytest tests/test_presets.py tests/test_roles.py tests/test_keybindings.py tests/test_model_switch.py tests/test_conv_index.py tests/test_web_model_name.py -q` → **4 failed, 326 passed, 6 errors**

- `test_web_model_name.py` (all 6 error): `setUpClass` runs `node tests/model_name_peer.mjs`, which does `import { nameOf, modelName } from '../app/modelname.js'` — `app/modelname.js` is now a CommonJS module (last touched by `1aa4d54a` "Rename models"), so node 18 rejects the named ESM import.
- `test_presets.py::GuiMirrorTests` (2 failed): the `src/Pane.h` GUI mirror drifted from the backend preset tables.
- `test_keybindings.py::GuiDefaultsTests` (2 failed, incl. `test_qwas_defaults` and `test_the_presets_doc_mirrors_the_shipped_tables`): default-key tables drifted.

Each cause postdates and is unrelated to #4BPE's change; the card's own work is unaffected. Full command lines and failure text in the evidence file.

## Done means
The six files run green at clean HEAD: either the mirrors are updated to what ships, or the app sources restored to what the mirrors expect; `model_name_peer.mjs` imports in a form node accepts from the module format `app/modelname.js` actually has.

## Tests
`PYTHONPATH=backend python3 -m pytest tests/test_presets.py tests/test_roles.py tests/test_keybindings.py tests/test_model_switch.py tests/test_conv_index.py tests/test_web_model_name.py -q` → all pass.
