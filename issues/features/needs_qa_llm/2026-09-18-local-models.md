---
id: 24XJ
type: work
status: needs-qa-llm
labels: [feature]
component: [providers, worker, gui]
milestone: desktop-alpha
workstream: providers
assignee: agent
implemented_by: Claude Fable 5.1 (Claude Code, with Claude Opus 5 subagents), 2026-09-18
rank: zzzz12
created: '2026-09-18'
acceptance: a model served on this machine (llama.cpp, Ollama, LM Studio or vLLM) can be Main, Flash or Lite with no API key, and a real agent turn with a tool call runs against Ternary Bonsai 2 27B
source: 'owner in chat, 2026-09-18: "research how opencode and other harnesses use local LLMs. in relay, set this up as a robust feature for agentic terminal use with local LLMs. install and set up bonsai 27B to run in relay as a test case."'
links: {plans: [], commits: [48c80f7, 82237ad, 2a819ed, 4d55a1a, dd35ead, 920aec7, 234bd4d, 9b47e6e], evidence: [docs/qa_evidence/2026-09-18-local-models/], related: [docs/LOCAL-MODELS.md, JH22, M109], github: null}
---
# Local models: an agent on a model this machine serves

## Issue
research how opencode and other harnesses use local LLMs. in relay, set this up as a robust feature for agentic terminal use with local LLMs. install and set up bonsai 27B to run in relay as a test case.

## Tasks
- [x] `backend/relay_core/localmodels.py`: the endpoint registry, the probe (llama.cpp `/health` and `/props`, Ollama `/api/tags`, `/v1/models`), and the four worker messages <!-- t:a1 -->
- [x] The keyless path: `provider_config`, `RoleResolver`, the `presets` event, the key Test button, and `max_tokens` clamped to the served window <!-- t:a2 -->
- [x] Transport for a local server: a first-token deadline apart from the idle stall, `stream_options.include_usage`, `parallel_tool_calls: false`, envelope repair, `<think>` split, the context-overflow message <!-- t:a3 -->
- [x] Tool calls written as text are recovered, per endpoint, off by default <!-- t:a4 -->
- [x] GUI: a local endpoint is a row in the model dropdown and can be pinned to a tier or a role <!-- t:a5 -->
- [x] `scripts/relay-local.py`, `relay-agent.py --provider local:<id>`, `docs/LOCAL-MODELS.md` <!-- t:a6 -->
- [x] Ternary Bonsai 2 27B installed under `/home/elliott/data/llms/`, served on 127.0.0.1:8080, unloaded when idle, and a real turn with a tool call run against it <!-- t:a7 -->
- [x] A Local models pane in Settings (health, Detect, model list) <!-- t:a8 -->
- [ ] Per-request Ollama `num_ctx`: needs the native `/api/chat`, a second transport <!-- t:a9 s=deferred -->
- [ ] Servers on the LAN: unencrypted traffic off this machine is the owner's decision <!-- t:aa s=deferred -->

## Decisions
- **A registry beside the presets, not new presets.** A local server's model id and its real
  window are not known ahead of time, and every invariant in `tests/test_presets.py` (https base,
  https key page, a window of 128K or more, a tier triple per preset, the `src/Pane.h` mirror)
  would have to be loosened for four entries that are mostly wrong. `localmodels.py` keeps
  `LocalEndpoint` records in `$XDG_CONFIG_HOME/relay/local-models.json`, written by the worker so
  `scripts/relay-agent.py` reads the same file with no GUI. Ids are `local:<slug>`: they cannot
  collide with a preset and `keystore._check_id` refuses them, so one can never reach the keyring.
- **Keyless means plain HTTP to a loopback host, never "no key was found".** A mistyped https
  endpoint keeps today's "No stored key for …" error instead of sending an unauthenticated request.
- **Everything the transport does differently is gated on `ProviderConfig.local`.** A cloud
  provider's request and its failure modes are byte for byte what they were.
- **Relay does not run the server.** A worker is the chosen OOM victim
  (`worker.prefer_as_oom_victim`) and a child holding the model would inherit that; the roadmap
  says no background daemons. A systemd user unit outside the repo serves the model, and when a
  probe finds nothing Relay prints the command that starts it.
- **The server unloads when idle and is not started at boot** (owner, 2026-09-18). It holds about
  10 to 16 GB of the shared 121 GiB while loaded; a 7 GB file reloads in seconds, and the
  first-token deadline (300 s for a local endpoint) covers the reload.
- **Ternary Bonsai 2 27B on PrismML's llama.cpp fork** (owner, 2026-09-18). No Bonsai 2 file runs
  on mainline llama.cpp. The fork is pinned to a recorded commit, bound to 127.0.0.1 and run
  without sudo. A wrong Bonsai file loads and writes fluent nonsense rather than failing, so a
  one-sentence check runs before the endpoint is registered.
- **Text recovery of tool calls is built and off by default** (owner, 2026-09-18). A parser that
  misfires turns a JSON block in an ordinary answer into an executed command. When an endpoint
  turns it on it fires only if the reply finished normally, carried no native tool calls, names a
  tool that was offered, and the block is the whole message.
- **BYOK still holds.** `docs/ROADMAP.md` says keys come from the environment or the keyring and
  Relay hosts nothing. A loopback server needs no key, and Relay neither hosts nor bills one.

## What landed
- `82237ad` backend: `localmodels.py` (registry, probe, worker protocol 23), `localtext.py`
  (`<think>` splitter, text tool-call recovery), the `local` paths of `provider.py`, the keyless
  path in `session_protocol.py`, `roles.py` and `keytest.py`, the `presets` event, and the four
  events withheld in `remote/wire.py`. 71 new test cases in four files.
- `2a819ed` `scripts/relay-agent.py --provider local:<id>`, `docs/LOCAL-MODELS.md`, and the
  ARCHITECTURE, VALIDATION and ROADMAP lines.
- `4d55a1a` detect waits for a server that is still loading, found by the sleeping-server check;
  the backend evidence.
- `dd35ead` holds the GUI change (`src/Pane.h`, `src/ModelSettings.cpp`,
  `tests/modelsettings_test.cpp`). It is another session's bulk commit that swept this work in
  before it was staged, so the GUI hunks have no commit of their own.
- Outside the repo: PrismML llama.cpp fork `1a07bfa5f` and `Ternary-Bonsai-2-27B-PQ2_0.gguf` under
  `/home/elliott/data/llms/`, `llama-bonsai.service` on `127.0.0.1:8080` (not enabled at boot,
  `--sleep-idle-seconds 600`), and `local:bonsai` in `~/.config/relay/local-models.json`.
- Protocol section 23 is written in `docs/LOCAL-MODELS.md`, not yet in
  `docs/AGENT-SESSIONS-PROTOCOL.md`: section 22 there was another session's uncommitted text when
  this landed, and a section appended after it could not be committed apart from it.

## Second round, the same day
The owner asked for a Settings section, an agent that sets a model up, and research on what is
current. What landed:
- `9b47e6e` **Options › Local models**: saved servers with status, Test, Refresh and Remove; Find
  servers; Add by address; the two per-endpoint switches; "Set up a model with the agent…".
- `234bd4d` the bundled **`local-model-setup` skill** (Relay's first bundled skill, in
  `backend/relay_core/skills_bundled/`), dated runtime and model recipes, and
  **`relay-local.py smoke`**: it answers, a native tool call, two consecutive calls, the literal
  strings `<tool_call>` and `</think>` explained in prose, plausible usage. Measured: Muse Glimmer
  through Ollama 5/5; Bonsai 2 on the PrismML llama-server 4/5 (the server's parser turns tags the
  user typed into a bogus call; Relay refuses a tool it did not offer, so it is a wasted step).
- `920aec7` `tool_arguments_as_object` per endpoint (Muse Glimmer's template on llama.cpp refuses
  JSON-string arguments), Meta's ATEM and DeepSeek's DSML in text recovery, and side calls
  (summaries, recaps, suggestions) keeping the local transport instead of rebuilding a hosted-style
  config.
- `#JH22` (`5ef8058`) the Local tier and `/local`; `#M109` (`74cac3e`) the provider dialog.
- Research, in `docs/LOCAL-MODELS.md` and the skill's recipes: DwarfStar is a runtime (antirez's
  `ds4`), not a model; GLM-5.3-Flash and DeepSeek V4/V4.1 Flash are open weights but too large or
  too unsettled to set up by default; Muse Glimmer 30B is the strongest small agent model found and
  already runs here through Ollama. The catalog is dated and the smoke test decides.

## For QA
- [ ] Options › Local models: Find servers lists what is running, Save adds it to every pane's dropdown, Remove takes it away
- [ ] "Set up a model with the agent…" opens an agent turn that loads `local-model-setup` and starts with a read-only survey
- [ ] `scripts/relay-local.py smoke http://127.0.0.1:8080 --model bonsai-2-27b` reports 4/5 with check 4 failing as described; against Ollama's `muse-glimmer:latest` 5/5
- [ ] With no key stored anywhere and `local:bonsai` saved, a new pane's model chip reads `bonsai-2-27b · local` and a prompt that needs a tool gets a tool call and an answer
- [ ] With one keyed preset, a new pane opens on the keyed preset, not the local row; picking the local row keeps the conversation and the context gauge takes 131,072
- [ ] Settings › Models › API keys has no local row; Model roles lets Flash or Lite be the local endpoint with no "(no key)"
- [ ] `systemctl --user stop llama-bonsai`, then a prompt: the pane says nothing is answering on 127.0.0.1:8080 and how to start it
- [ ] After ten idle minutes `relay-local.py probe` says `sleeping`; the next prompt answers without a stall error
- [ ] A hosted provider (Kimi or GLM) behaves as before: same request keys, a 60 s stall still stalls
- [ ] `tool_text_recovery` stays off for `local:bonsai`; turning it on for a model that writes `<tool_call>` text makes the call run, and an answer that only shows JSON is never run

## What the other harnesses do
opencode, Codex CLI, Crush, Zed, Aider, Goose, Cline, Continue and Qwen Code all point an
OpenAI-compatible client at a local base URL. What they differ on is what this card copies:
discovery instead of hand-declared models (Crush and Zed do, opencode has it as an open issue);
a short connect probe against a stream-idle budget of about 300 s (Codex, opencode, Qwen Code);
an error that says how to start the server (Codex); `stream_options.include_usage` forced on
(opencode); `<think>` stripped client-side (Aider); tool calls that land in `content` recovered
(LM Studio's default mode, Goose's toolshim, Cline's legacy XML). The full notes are in
`docs/LOCAL-MODELS.md`.
