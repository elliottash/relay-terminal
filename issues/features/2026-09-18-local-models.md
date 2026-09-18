---
id: 24XJ
type: work
status: in-progress
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
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Local models: an agent on a model this machine serves

## Issue
research how opencode and other harnesses use local LLMs. in relay, set this up as a robust feature for agentic terminal use with local LLMs. install and set up bonsai 27B to run in relay as a test case.

## Tasks
- [ ] `backend/relay_core/localmodels.py`: the endpoint registry, the probe (llama.cpp `/health` and `/props`, Ollama `/api/tags`, `/v1/models`), and the four worker messages <!-- t:a1 -->
- [ ] The keyless path: `provider_config`, `RoleResolver`, the `presets` event, the key Test button, and `max_tokens` clamped to the served window <!-- t:a2 -->
- [ ] Transport for a local server: a first-token deadline apart from the idle stall, `stream_options.include_usage`, `parallel_tool_calls: false`, envelope repair, `<think>` split, the context-overflow message <!-- t:a3 -->
- [ ] Tool calls written as text are recovered, per endpoint, off by default <!-- t:a4 -->
- [ ] GUI: a local endpoint is a row in the model dropdown and can be pinned to a tier or a role <!-- t:a5 -->
- [ ] `scripts/relay-local.py`, `relay-agent.py --provider local:<id>`, `docs/LOCAL-MODELS.md` <!-- t:a6 -->
- [ ] Ternary Bonsai 2 27B installed under `/home/elliott/data/llms/`, served on 127.0.0.1:8080, unloaded when idle, and a real turn with a tool call run against it <!-- t:a7 -->
- [ ] A Local models pane in Settings (health, Detect, model list) <!-- t:a8 s=deferred -->
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

## What the other harnesses do
opencode, Codex CLI, Crush, Zed, Aider, Goose, Cline, Continue and Qwen Code all point an
OpenAI-compatible client at a local base URL. What they differ on is what this card copies:
discovery instead of hand-declared models (Crush and Zed do, opencode has it as an open issue);
a short connect probe against a stream-idle budget of about 300 s (Codex, opencode, Qwen Code);
an error that says how to start the server (Codex); `stream_options.include_usage` forced on
(opencode); `<think>` stripped client-side (Aider); tool calls that land in `content` recovered
(LM Studio's default mode, Goose's toolshim, Cline's legacy XML). The full notes are in
`docs/LOCAL-MODELS.md`.
