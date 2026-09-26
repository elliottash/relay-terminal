# Local models: what the implementer ran (GUI), 2026-09-18

Implementer: Claude Opus 5 (1M context) subagent. Card `#24XJ`. The backend side of the same card
is in [`NOTES-BACKEND.md`](NOTES-BACKEND.md); this page is only the GUI.

## What changed

- `src/Pane.h`: the pane's list of selectable models (`m_stored`, built from the worker's `presets`
  event) admits a row with `"local": true` as well as one with a stored key. Everything that reads
  `m_stored` therefore follows: the model chip's dropdown, `/model` (the picker and the argument
  form), the subagent model menu, and the auto-configure order. A local row is *not* allowed to win
  the last step of that order ("the first stored key"); it is auto-picked only as the restored or
  saved `provider/preset`, or when no preset has a key at all. In the dropdown a local row reads as
  its model id followed by `· local`.
- `src/ModelSettings.cpp`: one `usable(preset)` helper — a stored key **or** `local` — now gates the
  roles modal's `choosableProviders()`, its tier provider combos, `pinRole()` and `rebuild()`, so a
  local endpoint is choosable there and is never labelled `(no key)`. `KeysDialog::rebuild()` was
  not touched: it lists the `subscription` / `aggregator` / `payg` groups and a local row's group is
  `local`, so it stays out of the API-keys modal (shot `c` below is the check).

Not changed: `Pane::voiceKeyStored()` still requires a real OpenRouter key, because voice
transcription runs on OpenRouter whatever the pane's model is, and a local chat endpoint cannot
serve it.

## The run

`drive.sh` in this folder. One Xvfb display, `build/relay`, `xdotool` and ImageMagick `import`, with
the profile fully isolated — `HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`,
`XDG_RUNTIME_DIR` and `TMPDIR` all inside a throwaway sandbox and `RELAY_KEYRING=off`, so the
owner's own live Relay cannot be confused with this one. `RELAY_LOCAL_MODELS` points at a registry
holding one endpoint, written by

    RELAY_LOCAL_MODELS=<path> python3 scripts/relay-local.py add --id bonsai \
        --label "Bonsai 2 27B" --base-url http://127.0.0.1:8080 --detect

against the live `llama-bonsai.service` (`bonsai-2-27b` on `127.0.0.1:8080`). The workspace holds
three files with names a model cannot guess — `ledger-ferrous.md`, `quokka-notes.txt`,
`zamboni.cfg` — so an answer that names them proves the tool call really ran.

### With no key anywhere (no `RELAY_*_API_KEY`, empty keyring)

| Shot | What it shows |
|---|---|
| `implementer-a-pane-on-the-local-model.png` | the pane as it opens: the model chip reads **bonsai-2-27b · local**. Nothing has a key, so the local endpoint is the automatic choice and there is no "No stored provider keys" message. |
| `implementer-b-model-dropdown.png` | the dropdown: one preset row, `bonsai-2-27b · local`, then the Main/Flash agent rows (both on `bonsai-2-27b`) and Model options…. No other preset is offered, because none has a key. |
| `implementer-c-keys-modal-has-no-local-row.png` | the API-keys modal: Subscriptions / Aggregator / Pay-as-you-go, every row "Not set", **no local row and no "On this machine" group**. |
| `implementer-d-roles-modal-local-provider.png` | Model roles: Default provider is **Bonsai 2 27B**, with no `(no key)` suffix, and Main/Flash/Lite all resolve to `bonsai-2-27b`. |

### With one fake `RELAY_KIMI_API_KEY` beside the same endpoint

Nothing is ever sent to Kimi: `configure` makes no network call, and the only prompt is asked after
the pane has been switched to the local model.

| Shot | What it shows |
|---|---|
| `implementer-e-a-key-wins-the-automatic-choice.png` | the pane opened on **kimi-k3**, not on the local endpoint: a local row never wins the automatic "first stored" step over a preset a key can reach. |
| `implementer-f-dropdown-with-a-key-and-the-local-row.png` | the dropdown: `kimi-k3` first, then `bonsai-2-27b · local`; the ticked Main agent is still `kimi-k3`. |
| `implementer-g-switched-to-the-local-model.png` | the local row picked by hand: the chip is on `bonsai-2-27b · local`, the conversation was kept (`set_model`, not a reconfigure), and the context gauge moved to the endpoint's own 131,072-token window. |
| `implementer-h-agent-turn-with-tool-call.png` | a real turn on the local model. `*list the files in the workspace directory and tell me their names` → reasoning, a `list_directory` call, and the answer **ledger-ferrous.md, quokka-notes.txt, zamboni.cfg** — the three names only the tool could know. 2 tool calls, 22 s. The local model also wrote the pane title ("List workspace files"). |

`relay-stderr-*.log` are the three Relay lifetimes' stderr (one per scenario; a fresh process per
modal, because closing one dialog and opening the next from the palette lost the X connection under
Xvfb and the shot after it was of nothing). The two lines in
`relay-stderr-one-key-and-local.log` are the harness, not the product: the script's `trap` deletes
the sandbox while that last Relay is still shutting down, so the scrollback save and the window lock
find their directory gone.

## What was not proved here

These are implementer checks, not QA. The keyring was off throughout, so "no key is stored" is a
property of this sandbox rather than of a machine that has keys; the Kimi key was a fake string and
was never used for a request; and only one local server (llama.cpp) was exercised — Ollama, LM
Studio and vLLM rows go through the same code but were not run.

---

# Settings section: what the implementer ran (GUI), 2026-09-18

Implementer: Claude Opus 5 (1M context) subagent, a later pass on the same card `#24XJ`. Appended,
not a rewrite: everything above is the first GUI pass (the dropdown, the two modals, a turn on the
local model). This part is only the new **Settings › Local models** section, which replaces
`scripts/relay-local.py` as the way in.

## What changed

- `src/LocalModelsSettings.{h,cpp}` (new, library `relay-localmodels`): the section. It owns no
  widgets — it holds what the worker last said and turns it into a `SettingsSection`, so the
  Options pane's row model stays the only way rows are drawn and the whole thing is testable
  headlessly. It speaks protocol 23 (`local_endpoints`, `local_probe`, `local_endpoint_save`,
  `local_endpoint_delete`) plus `test_key` for a `local:` preset, keyed by request id
  (`lm-endpoints`, `lm-ep:<id>`, `lm-find:<port>`, `lm-addr`, `lm-save:<id>`, `lm-find-save`,
  `lm-addr-save`) so every answer finds its own row.
- `src/SettingsPane.{h,cpp}`: one new row kind, `Buttons` (several buttons on one row, for a row
  that stands for a thing rather than a value — Test · Refresh · Remove), and `onSectionShown`,
  which fires once when a section's tab comes to the front. That is what the probing hangs off:
  a probe wakes a sleeping server, so it happens on arrival and on an explicit Refresh or Find
  servers, never on a timer.
- `src/RelayWindow.h`: the section is pushed right after Models; the controller's `send` goes
  through the active pane (`Pane::sendLocalModelRequest`) — the same worker connection the keys
  dialog uses, not a second one — and a save or a delete asks every pane in the window for
  `presets` again, which is what puts the row into or out of each pane's model dropdown. The
  action `agent.localModelSetup` hands the active pane one prompt.
- `src/Pane.h`: the four `local_*` events, a `key_tested` whose preset starts `local:`, and an
  `error` carrying an `lm-` request id go to `onLocalModelEvent` instead of the transcript;
  `askAgent()`, `sendLocalModelRequest()` and `refreshPresets()` are the three things the window
  needs from a pane.
- `src/Keymap.h`: `agent.localModelSetup`, with no default shortcut — it is a once-per-machine
  errand and Options › Local models is the way in. No shortcut hint was added: RELAY.md's rule is
  for a feature that *has* a fast path, and this one has none.

## The run

`drive-settings.sh` in this folder, driven step by step so each shot could be looked at before the
next keystroke. One Xvfb display, `build/relay`, `xdotool` and ImageMagick `import`, profile fully
isolated (`HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`, `XDG_RUNTIME_DIR`,
`TMPDIR`, `RELAY_KEYRING=off`) and `RELAY_LOCAL_MODELS` pointing at a path that did not exist, so
the section started genuinely empty. No `RELAY_*_API_KEY`. The owner's two servers were used as
they were found and neither was started, stopped or reconfigured.

| Shot | What it shows |
|---|---|
| `implementer-settings-a-empty-section.png` | the Options pane with a **Local models** tab between Models and Terminal: "No local endpoints yet. Find a server below, or add one by address.", then Find servers, Add by address and "Set up a model with the agent…". |
| `implementer-settings-b-find-servers.png` | Find servers: **Ollama · http://127.0.0.1:11434/v1 — muse-glimmer:latest · 131,072 tokens · ready** and **llama.cpp · http://127.0.0.1:8080/v1 — bonsai-2-27b · 131,072 tokens · ready**, each with Save, and "Nothing on 8000, 1234." The four probes went out only because the button was pressed. |
| `implementer-settings-c-saved-row.png` | Save on the llama.cpp row (`local_endpoint_save` with `detect: true`): the endpoint row reads **llama.cpp · bonsai-2-27b · 131,072 tokens · ready** with Test · Refresh · Remove, the two toggles under it, and the pane's model chip has already become `bonsai-2-27b · local` — the save asked every pane for `presets` again. |
| `implementer-settings-d-test.png` | Test: **"Test: answered in 48738 ms."** llama-server had gone to sleep, so the 48 s is the reload; `test_key` waits a model load out, unlike a probe. |
| `implementer-settings-e-model-dropdown.png` | the pane's model dropdown: **bonsai-2-27b · local**, then the Main / Flash / Local agent rows. |
| `implementer-settings-f-removed.png` | Remove: the section is empty again, the registry file holds `"endpoints": []`, and the composer's chip is back to "No stored keys". |
| `implementer-settings-g-add-by-address-and-toggles.png` | Add by address: `http://127.0.0.1:11434` → Detect → **Ollama · muse-glimmer:latest · 131,072 tokens · ready** → Save, then both per-endpoint toggles on. The registry afterwards: `tool_text_recovery: true`, `tool_arguments_as_object: true` — the rows are drawn from the endpoint the worker echoes back, so a key the registry drops shows as a toggle that goes back to off. |
| `implementer-settings-h-setup-with-agent.png` | "Ask the agent…": the pane received `Load the local-model-setup skill and set up a local model on this machine for me.` as an ordinary agent request (violet ✦ line, "Settings · Local models" under it) and the agent answered `loaded skill local-model-setup · 176 lines · 6 files`. |

`relay-stderr-settings.log` is that Relay's stderr; it is empty.

## What was not proved here

Implementer checks, not QA. `tool_arguments_as_object` was already in the backend when this ran, so
the "a toggle the registry does not know goes back to off" path was exercised only by the unit test,
not on screen. Only llama.cpp and Ollama were exercised — LM Studio and vLLM go through the same
code and were not running. The "not running" status word and the error sentence under a down
endpoint were seen in the unit test and in Find servers' silent ports, not on a saved row whose
server was stopped (stopping the owner's servers was out of bounds). Nothing here says whether the
section behaves with two Relay windows open.
