# A Local tier and /local: what the implementer ran, 2026-09-18

Implementer: Claude Opus 5 (1M context) subagent under Claude Fable 5.1. Card `#JH22`. The local
endpoint feature this builds on is card `#24XJ` (`docs/qa_evidence/2026-09-18-local-models/`).

## What changed

Backend
- `backend/relay_core/presets.py`: `local` is a fourth tier — `TIERS` is four wide, `PROVIDER_TIERS`
  names the three that come from a provider's table, and `TIER_DEFAULTS` is untouched. `tier_fallbacks`
  special-cases it: `("local", "main")`, never through Lite and Flash.
- `backend/relay_core/roles.py`: `local` is also a pane role (`ROLE_TIERS["local"] = "local"`).
  `_tier_entry` resolves the tier from `tiers.local`, else the first endpoint in
  `localmodels.catalog()`; `_tier` returns Main with the note `"No local model is set up; using
  Main."` when there is none. `validate_tiers` accepts only a saved `local:<slug>` id or a
  plain-http loopback `base_url` + `model` for this tier.

GUI
- `src/Pane.h`: `/local` beside `/main` and `/flash`, the `role:local` row in the model chip (only
  when an endpoint exists), `hasLocalEndpoint()`, `toggleLocalAgent()`, the role id and label, and
  the hint that names `/local` on the slow path.
- `src/ModelSettings.cpp`: a fourth tier row whose combo lists local endpoints only — no Model…,
  no effort box — storing `tiers/local/preset`; disabled with "No local model is set up." when the
  registry is empty.
- `src/Keymap.h` (`agent.localAgent`, no default key) and `src/RelayWindow.h` (its dispatch and a
  palette row shown only when an endpoint exists).

Not changed: the advanced "Bring your own key" dialog (`Pane::configure()`). The parent session is
editing it; nothing in that function was touched here.

## The run

`drive.sh` in this folder, then `drive-rest.sh` for the last three shots (see below). One Xvfb
display, `build/relay`, `xdotool` and ImageMagick `import`, with the profile fully isolated —
`HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR` all
inside a throwaway sandbox and `RELAY_KEYRING=off` — so the owner's live Relay on `:0` cannot be
confused with this one. Neither script starts or stops `llama-bonsai.service`.

One fake `RELAY_KIMI_API_KEY=not-a-real-key-kimi`, so the pane opens on a hosted Main (`kimi-k3`)
and the switch to the local model is visible. Nothing is ever sent to Kimi: `configure` makes no
network call and the only prompt is asked after the pane is on the local model.

`RELAY_LOCAL_MODELS` points at a registry written by the product's own CLI against the live server:

    scripts/relay-local.py add --id bonsai --label "Bonsai 2 27B" \
        --base-url http://127.0.0.1:8080 --detect

Its output is `registry-add.txt`: llama.cpp, `bonsai-2-27b`, window 131,072, tools and thinking —
detected, not typed. The workspace holds three files with names a model cannot guess
(`ledger-ferrous.md`, `quokka-notes.txt`, `zamboni.cfg`), so an answer that names them proves the
tool call really ran.

### With one saved endpoint

| Shot | What it shows |
|---|---|
| `implementer-a-pane-on-hosted-main.png` | the pane as it opens: the chip reads `kimi-k3`, the hosted Main. |
| `implementer-b-dropdown-has-the-local-row.png` | the model chip's dropdown: the two presets (`kimi-k3`, `bonsai-2-27b · local`), then the pane-role rows — `✓ Main agent · kimi-k3`, `Flash agent · kimi-k2.7-code-highspeed` and the new `Local agent · bonsai-2-27b` — then Model options…. |
| `implementer-c-slash-local-typed.png` | `/local` typed in the composer, selected at the top of the slash popup: "Run this pane on a model served on this machine". The `/local-model-setup` skill under it is another session's bundled skill, landing in the same list; the exact match ranks first, so Enter runs `/local` (shot `d`). |
| `implementer-d-pane-on-the-local-agent.png` | after Enter: the chip reads **Local agent · bonsai-2-27b**, the toast says "Local agent: bonsai-2-27b · conversation kept", and the context gauge has moved to the endpoint's own window. |
| `implementer-e-turn-with-a-tool-call.png` | a real turn on the local model. `*list the files in the workspace directory and tell me their names` → reasoning, `listed ./ · 3 entries`, and the answer **ledger-ferrous.md, quokka-notes.txt, zamboni.cfg** — the three names only the tool could know. 1 tool call, 14 s. |
| `implementer-f-back-on-main.png` | `/main`: the chip is back on `kimi-k3`, "Model: kimi-k3 · conversation kept", and the transcript above is unchanged. |
| `implementer-g-roles-modal-local-row.png` | Model roles: a fourth row, **Local · bonsai-2-27b**, whose only control is the endpoint combo ("Bonsai 2 27B") — no Model… and no effort box, unlike Flash and Lite. |

### With an empty registry

| Shot | What it shows |
|---|---|
| `implementer-h-no-local-model-is-set-up.png` | `/local`: "No local model is set up. Settings › Local models, or scripts/relay-local.py add --detect." The chip still reads `kimi-k3` — the pane did not switch. |
| `implementer-i-roles-modal-local-row-disabled.png` | Model roles: the Local row greyed, its combo reading "No local model is set up." and the inline note "No local model is set up; using Main." |

### Why there are two scripts

`drive.sh` reached shot `f` and then failed to start its next Relay: a sibling session relinked
`build/relay` at that moment and the path was briefly gone (`relay-stderr-roles-with-endpoint.log`
from that attempt held "No such file or directory"). `drive-rest.sh` re-runs the last three
scenarios, each with its own fresh Relay, under `build/.relay-build.lock` so the binary cannot move
again. Same isolation, same harness. The two `Killed` lines it prints are its own `kill -9` of the
previous Relay between scenarios.

## Tests

- `RELAY_KEYRING=off RELAY_LOCAL_MODELS=/nonexistent/x.json PYTHONPATH=backend:. python3 -m unittest
  tests.test_roles tests.test_presets tests.test_keybindings tests.test_local_keyless
  tests.test_configure_provider tests.test_agent tests.test_local_tier` → **160 passed**.
- `ctest --test-dir build -E backend-and-bash` → **41/41 passed**; the modelsettings binary reports
  13 cases, three of them new (the Local row's contents, its disabled state, and a job in Advanced
  following the Local tier).

## What was not proved here

These are implementer checks, not QA. The keyring was off throughout, the Kimi key was a fake string
and was never used for a request, and only one local server (llama.cpp) was exercised. A server that
is *down* while an endpoint is saved was not exercised here — the transport path for that is card
`#24XJ`'s and unchanged — and neither was a restart restoring a pane left on the Local agent.
