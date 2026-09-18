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
