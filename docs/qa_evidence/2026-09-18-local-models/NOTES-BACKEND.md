# Local models: what the implementer ran (backend), 2026-09-18

Implementer: Claude Fable 5.1, with Claude Opus 5 subagents for the install and the GUI. Card `#24XJ`.

## The server
Ternary Bonsai 2 27B (`Ternary-Bonsai-2-27B-PQ2_0.gguf`, 7,206,168,928 bytes) on PrismML's
llama.cpp fork, branch `prism`, commit `1a07bfa5f4144274c8f1c9963821dd9d9a51854b`, built on the
DGX Spark with `-DGGML_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=121`. `llama-bonsai.service` (systemd
user unit, not enabled at boot) serves it on `127.0.0.1:8080` with `-c 131072 --jinja
--reasoning-format deepseek -np 1 --sleep-idle-seconds 600`.

- `implementer-gibberish-gate.txt`: the one-sentence check that the file and the build agree. A
  Bonsai file on the wrong build writes fluent nonsense instead of failing.
- `implementer-toolcall-smoke.txt`: raw `/v1/chat/completions` with one tool. `tool_calls` is
  native, `arguments` is a string, an `id` is present, reasoning is in `reasoning_content`.

## Relay against it
- `scripts/relay-local.py scan` found llama.cpp on 8080 (`bonsai-2-27b`, window 131,072, tools,
  thinking) and Ollama on 11434.
- `scripts/relay-local.py add --id bonsai --base-url http://127.0.0.1:8080 --detect` saved
  `local:bonsai` in `~/.config/relay/local-models.json`.
- `provider_config({"preset": "local:bonsai", "use_stored_key": True})` gave `local=True`, no key,
  `first_token_timeout=300`, window 131072; the agent's context tracker took the same window.
- One turn, "list the files, then read note.txt": `list_directory`, then `read_file`, then a
  correct answer; `usage` arrived on all three steps (2504/117, 2658/45, 2799/170 tokens), the
  reasoning went to the thinking channel, 17.5 s.
- `scripts/relay-agent.py --provider local:bonsai --yes --prompt "Run 'wc -c note.txt' …"` ran the
  command through `run_command` and answered 17 bytes, which is right.
- `implementer-sleeping-server.txt`: a second instance on 8081 with a 20 s idle timer.
  `add --detect` was started while the model was still loading and waited for it. After the
  timer the probe said `sleeping` without waking it. A turn then woke it and finished in 20 s;
  used memory went from 40 GiB to 57 GiB. The first run of this check failed at the first step
  ("needs a model id" for a loading server), which is why `detect` now waits.

## Tests
`./scripts/test.sh`: the new files are `tests/test_localmodels.py`, `test_localtext.py`,
`test_provider_local.py` and `test_local_keyless.py` (71 cases). On 2026-09-18 the full suite had
four failures in `test_remote_gui_host`, `test_remote_terminal` and `test_tools`, all in files
other sessions had uncommitted work in; none touch this change, and the 241 tests of the
provider, preset, role, keystore, agent and wire modules passed on the exact tree committed.

## Not done here
The GUI run is in `NOTES-GUI.md`. No second model family was tried, so text recovery and the
`<think>` splitter are covered by tests only: Bonsai under `--jinja --reasoning-format deepseek`
needs neither.
