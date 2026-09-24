---
id: E516
type: work
status: needs-qa-llm
assignee: agent
implemented_by: glm/glm-5.3
rank: zzzzzzzzzzw
created: '2026-09-19'
links: {plans: [], commits: [c0734bdd2232f8b86f7603c1bdbc3822cf816898], evidence: [docs/qa_evidence/2026-09-20-codex-model-dropdown/], related: [], github: null}
---
# add model selection in codex options

## Issue
add model selection in codex options (right now its a text field). you can pick 5.6 luna, 5.6 terra, 5.6 sol, and 6 astra.

## Plan
**Goal.** Options › Claude Code and Codex › Codex › Model stops being a free-text field and becomes a dropdown offering the codex models: 5.6 luna, 5.6 terra, 5.6 sol, and 6 astra (plus "Default (the CLI's own)").

**Findings.**

- The Options section is built in `src/RelayWindow.h` (~lines 1663–1760, the "Options › Claude Code and Codex" block). The Model row is a `choiceRow` (dropdown) **only when the codex preset row carries a non-empty `models` array**; when it is empty, line ~1715 (`if (models.isEmpty())`) builds a `textRow` — the text field the card is about. The comment there says why: "No list yet (codex's catalog is fetched in the background) or no worker at all".
- `preset.models` comes from the worker: `backend/relay_core/guest_harness_provider.py`, `guest_models("codex")`. It is filled **once per worker process, in a background thread**, by running `codex debug models` (`_read_codex_catalog`, parsed by `guest_harness_codex.catalog_rows`, which reads both the snake_case `debug models` and camelCase `model/list` shapes). Any failure — missing binary, non-zero exit, timeout (20 s), unparseable JSON — is logged at debug and leaves the catalogue **empty forever** ("a missing, refusing or slow codex is simply no menu").
- The current contract is tested in `tests/test_guest_harness_provider.py`, `CatalogueTests`: `test_a_codex_that_fails_or_is_missing_is_simply_no_menu` asserts the empty-catalogue = no-menu behaviour; `test_the_real_reader_parses_what_codex_debug_models_prints` covers the parser against a fixture. So the text field is what you get whenever the scan yields nothing on this machine.
- Claude's row already always has a list (`_CLAUDE_MODELS` from the adapter), so this is codex-only.
- Downstream of a pick, nothing changes: `guests/codex/model` in QSettings is what the harness route (`guest_options` → `thread/start`'s `model`, or `-m` on the Tier B launch) already consumes, and a stored value the list does not know is already appended to the dropdown (`RelayWindow.h` ~1739, "typed by hand earlier"), so existing settings survive.

**Steps.**

1. **Diagnose on this machine first.** Run `codex debug models` and check: (a) it exits 0 and prints the four models; (b) the exact slugs (`slug`/`id`) and `display_name`s for 5.6 luna / 5.6 terra / 5.6 sol / 6 astra — the plan cannot guess these and the dropdown's values must be the slugs `thread/start` accepts. If the command fails or the installed codex lacks `debug models`, that explains the text field and step 2 is the fix. If it *works*, also check whether the scan's parse drops the rows (log line "codex catalogue could not be read", or rows filtered by `hidden`/`visibility` in `catalog_rows`) and fix that instead of or as well as the fallback.
2. **Add a codex fallback catalogue** in `guest_harness_provider.py`: a module-level `_CODEX_FALLBACK_MODELS` list of the four models as contract rows (`{"id": <slug>, "label": <display name>, "efforts": [...], "default_effort": ...}`, efforts per model when known, else the `_CODEX_EFFORTS` union). `guest_models("codex")` returns the scanned catalogue when it has rows, the fallback when it is empty. This automatically fixes both surfaces that read `preset.models` (Options and the pane's model box).
3. **GUI:** no structural change needed — the `choiceRow` path already exists. Just confirm the text-field branch at `RelayWindow.h` ~1715 remains only for the genuinely list-less cases (no worker, codex not installed), and that the row's detail text still makes sense.
4. **Update the tests** in `tests/test_guest_harness_provider.py`: `test_a_codex_that_fails_or_is_missing_is_simply_no_menu` becomes "a codex that fails or is missing gets the fallback menu" (assert the four slugs/labels in `preset_rows()`'s codex `models`); add a test that a non-empty scanned catalogue wins over the fallback. Keep `test_the_first_presets_answer_does_not_wait_for_codex` passing — note the fallback must **not** appear before the scan has finished (`catalog_ready`), or reopening Options would flash the fallback then the real list; decide per what that test asserts and whether a brief text field is acceptable. Recommended: serve the fallback only once the scan has completed empty.

**Risks / open questions.**

- The four slugs must come from this machine's `codex debug models` output (step 1); a wrong id fails later at `thread/start` in codex's own words, which is handled but ugly.
- A hard-coded fallback can go stale as codex adds/retires models; the scanned catalogue always wins when present, so staleness only shows when the scan is broken — acceptable, and the fallback comment should say which codex version it was read from.
- If step 1 shows the scan actually works on this machine, the real bug is elsewhere (timing or parse) and the fallback is belt-and-braces; report which it was in the evidence.

**Verify.**

- `python3 -m pytest tests/test_guest_harness_provider.py -k "catalogue or preset"` (plus the full file).
- Live, under Xvfb with an isolated `XDG_CONFIG_HOME`: open Options › Claude Code and Codex, confirm the Codex Model row is a dropdown listing Default + the four models, pick one, and confirm it persists (`guests/codex/model` in QSettings) and a pane launched on Codex starts on it (status line / `-m` in the launch argv).

## QA checklist
- [ ] Live, under Xvfb with an isolated `XDG_CONFIG_HOME`: open Options › Claude Code and Codex — the Codex › Model row is a dropdown (not a text field) listing Default + gpt-6-astra, gpt-5.6-sol, gpt-5.6-terra, gpt-5.6-luna (plus gpt-5.5 when the scan serves the real catalogue).
- [ ] With Options already open from before the worker started (or reopened immediately), the row turns from text field into the dropdown by itself within ~a second — the pushed `presets` event, no re-ask.
- [ ] Pick `GPT-5.6-Luna`, reopen Options: still selected (`guests/codex/model` in QSettings); a pane started on Codex reports the model (status line / `-m`).
- [ ] Fallback path (only if cheap to fake): with `RELAY_CODEX_BIN` pointing at a failing binary, the dropdown still offers the four fallback models after the scan gives up.
- [ ] `PYTHONPATH=backend python3 -m unittest tests.test_guest_harness_provider` (the owner skipped suites at implement time; the two catalogue/worker classes passed: 9 OK).
