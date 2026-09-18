# Local models

Relay's agent can run on a model served on this machine: llama.cpp (`llama-server`), Ollama,
LM Studio, vLLM, or anything else that speaks OpenAI `/v1/chat/completions` on a loopback
address. No key, no account, nothing leaves the machine. Card `#24XJ`; code in
`backend/relay_core/localmodels.py`, `localtext.py` and the `local` paths of `provider.py`.

## Use one

```sh
scripts/relay-local.py scan                       # what is serving on 11434, 1234, 8080, 8000
scripts/relay-local.py add --base-url http://127.0.0.1:8080 --id bonsai --label "Bonsai 2 27B" --detect
scripts/relay-local.py list
scripts/relay-agent.py --provider local:bonsai    # the same agent, in a terminal
```

`--detect` asks the server for its model id, its kind, the context window it was *started* with
and whether its chat template takes tools. The endpoint is then a row in every pane's model
dropdown, and in Settings › Models › Model roles it can be Main, Flash or Lite or be pinned to one
role. It never appears in the API keys dialog: there is no key.

The registry is `$XDG_CONFIG_HOME/relay/local-models.json` (`RELAY_LOCAL_MODELS` overrides the
path; `scripts/test.sh` points it at a temp file). Each entry:

| Field | Default | Meaning |
|---|---|---|
| `id` | from the label | `local:<slug>`. The colon keeps it apart from every preset id, and the keystore refuses it. |
| `base_url` | | `http://` on `localhost`, `127.0.0.1` or `::1` only. |
| `model` | detected when the server lists one | The id the server expects. |
| `server` | detected | `llamacpp`, `ollama`, `lmstudio`, `vllm`, `openai-compatible`. |
| `context_window` | detected, else 32768 | The **served** window. Compaction and the overflow message use it. |
| `first_token_timeout` | 300 | Seconds allowed before the first token. Covers a cold load and a long prefill. |
| `parallel_tool_calls` | false | When false, `parallel_tool_calls: false` is sent with the tools. |
| `tool_text_recovery` | false | Recover tool calls the model writes as text. See below before turning it on. |
| `extra` | `{}` | `temperature`, `top_p` and the other keys `ProviderConfig` allows. |

If the server is restarted with a different `-c`, run `add --detect` again: the window is read
when the endpoint is saved, not on every turn.

## What Relay does differently for a local server

Everything here is behind `ProviderConfig.local`, which is true only for plain HTTP to a loopback
host. A hosted provider's request and its failures are byte for byte what they were.

- **No key.** None is looked up, none is sent, even if one was pasted. The rule is the URL, not
  the missing key: an `https` endpoint without a key still fails with "No stored key".
- **Two deadlines.** A hosted provider gets one idle deadline (60 s). A local server gets
  `first_token_timeout` until the first usable chunk and the idle deadline after it, because
  loading weights and reading a long prompt produce no bytes at all.
- **A loading server is waited for.** `llama-server` answers 503 "Loading model" until the weights
  are in; Relay retries every 2 s inside the first-token budget and says so in the pane.
- **Nothing listening says how to start it**, by port: `ollama serve`, `lms server start`,
  `vllm serve`, or `llama-server … --jinja`.
- **`stream_options: {"include_usage": true}`.** llama.cpp and Ollama stream no usage otherwise,
  and the context tracker would fall back to four characters a token.
- **Envelope repair.** `arguments` as a JSON object instead of a string, a missing `id` or `type`,
  a repeated id. A hosted provider doing any of these is still an error.
- **`<think>` tags become reasoning**, including a tag cut in two by a chunk boundary, an opener
  that is never closed (the rest is reasoning), and a closer with no opener (a template that opens
  the tag in the prompt). With `llama-server --reasoning-format deepseek` the server already does
  this and the splitter never fires.
- **Context overflow is named.** llama.cpp answers 400 when the prompt no longer fits (context
  shift is off by default). Relay matches the phrase, never quotes the body, and says to `/compact`
  or restart with a larger `-c`.
- **The output limit fits the window.** `max_tokens` is clamped to a quarter of the served window,
  so a 32K server is not promised a 32K reply.
- **The prompt prefix is stable.** `Agent.system_prompt()` and the tool list do not change between
  turns, which is what llama.cpp's `cache_prompt` (on by default) needs to skip re-reading them.

### Tool calls written as text

Some models, and some servers started without the right template, write a tool call into the
answer: `<tool_call>{…}</tool_call>` (Hermes, Qwen), Qwen3-Coder's `<function=…><parameter=…>`
XML, LM Studio's `[TOOL_REQUEST]…[END_TOOL_REQUEST]`, a fenced JSON block. With
`tool_text_recovery: true` Relay turns these back into tool calls. It is off by default because a
parser that misfires runs a command out of an ordinary answer. When on, it is all or nothing: the
reply finished normally, carried no native tool calls, consists of call blocks and whitespace
only, every block parses, and every name is a tool that was offered. Try fixing the server first:
for llama.cpp that is `--jinja`, and `--chat-template-file` if the GGUF's template is wrong.

## Serving a model

Relay does not start or stop servers. A worker is the process the OOM killer is told to prefer,
and the roadmap says no background daemons. `scripts/relay-local.py unit` prints a systemd user
unit for `llama-server`; it installs nothing.

Flags that matter for an agent, for `llama-server`:

| Flag | Why |
|---|---|
| `--jinja` | Tool calls come back as `tool_calls`. Without it they are text. |
| `--reasoning-format deepseek` | Reasoning arrives in `reasoning_content`, not in the answer. |
| `-c N` | The window. 32K is the floor for tool use; 64K and up is comfortable. |
| `--alias NAME` | A stable model id instead of a file path. |
| `-np 1` | One slot gets the whole window. |
| `--sleep-idle-seconds N` | Frees the model after N idle seconds, reloads on the next request. `/props` does not count as activity. |
| `--host 127.0.0.1` | Relay only talks to loopback, and nothing else should reach the server. |
| no `-ctk q4_0` | llama.cpp's own docs warn that extreme KV quantisation degrades tool calling. |

Ollama: set `OLLAMA_CONTEXT_LENGTH` (its `/v1` endpoint cannot take a window per request), and use
a model whose `capabilities` include `tools`. vLLM: `--enable-auto-tool-choice` with the
`--tool-call-parser` for the model family, or tool calls never arrive.

## The test case: Ternary Bonsai 2 27B on a DGX Spark

Installed 2026-09-18 under `/home/elliott/data/llms/` (nothing machine-specific is in this repo;
`README.md` there has the commands).

- **Model.** `prism-ml/Ternary-Bonsai-2-27B-gguf`, `Ternary-Bonsai-2-27B-PQ2_0.gguf`, 7.2 GB: a
  ternary quantisation of Qwen3.8-27B, 262K trained context, native tool calling and reasoning.
- **Runtime.** PrismML's llama.cpp fork, branch `prism`, commit `1a07bfa5f` (build b10706), built
  from source for GB10: `-DGGML_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=121`. No Bonsai 2 file runs on
  mainline llama.cpp, and a Bonsai file on the wrong build loads and writes fluent nonsense rather
  than failing, so a one-sentence check ran before the endpoint was registered.
- **Service.** `llama-bonsai.service` (systemd user unit), `127.0.0.1:8080`, `-c 131072`,
  `--sleep-idle-seconds 600`, not enabled at boot: `systemctl --user start llama-bonsai`.
- **Measured.** About 17 GiB held while loaded, about 14 GiB released on sleep, 1.6 s to reload,
  3.1 s cold start from page cache, 24 tokens/s decode. Tool calls are native, `arguments` is a
  string, ids are present, reasoning is in `reasoning_content`, `stream_options` and
  `parallel_tool_calls` are accepted, and a usage chunk arrives.
- **In Relay.** `relay-local.py add --detect` found llama.cpp, `bonsai-2-27b`, window 131,072,
  tools and thinking. A turn that listed a directory, read a file and answered took 17.5 s with
  usage on every step. Evidence: `docs/qa_evidence/2026-09-18-local-models/`.

## Worker protocol (section 23)

Four messages, handled by `localmodels.handle`. Each gets exactly one event back. All four events
are withheld from a remote client (`remote/wire.py`), like `presets`: what serves on the desktop's
loopback ports is provider configuration.

| Message | Reply |
|---|---|
| `local_probe {id?, base_url}` | `local_probed {id, base_url, ok, server, state, context_window, models: [{id, context_window, tools, thinking}], error?}`. `state` is `ready`, `loading`, `sleeping` or `down`. Runs on its own thread, 2 s per request, loopback only, no `Authorization`, no redirects. `base_url` in the reply is the OpenAI base (`…/v1`) whatever was sent. `tools` and `thinking` are `null` when the server does not say. |
| `local_endpoints {id?}` | `local_endpoints {id, items: [...]}`. No network. |
| `local_endpoint_save {id?, endpoint, detect?}` | `local_endpoint_saved {id, endpoint, probe?}`. With `detect: true` the server is probed first and fills `base_url`, `server`, `model` (when it serves exactly one), `context_window`, `tools`, `thinking`; a probe that finds nothing answers with `error`. |
| `local_endpoint_delete {id?, endpoint_id}` | `local_endpoint_deleted {id, endpoint_id, removed}` |

The `presets` event lists saved endpoints after the built-in presets, with the keys of a preset row
plus `local: true`, `server`, `group: "local"`, `has_stored_key: false`, `key_source: "local"`,
`efforts: []`, `tools`, `thinking`, `first_token_timeout`, `parallel_tool_calls` and
`tool_text_recovery`. Built-in rows carry `local: false`.

`configure`, `set_model`, a `tiers` entry, a `roles` entry and `test_key` all accept
`preset: "local:<id>"`. `use_stored_key` is ignored for one: there is no key. `test_key` on a local
endpoint makes the same two-word call and means "reachable and answering"; unlike a probe it waits
out a model load.

## How other harnesses do it

All of them point an OpenAI-compatible client at a local base URL. They differ in what they ask
the user to declare by hand and in what they do when the model misbehaves.

| Harness | Local setup | Worth knowing |
|---|---|---|
| opencode | `@ai-sdk/openai-compatible` with a `baseURL`; models, `limit.context` and `tool_call` declared by hand | No model discovery (an open issue). `headerTimeout` and `chunkTimeout` default to 300 s. Forces `include_usage`. `interleaved` names the JSON field that carries reasoning. |
| Codex CLI | `--oss`, built-in `ollama` and `lmstudio` providers | 5 s probe of `/v1/models` or `/api/tags`, 300 s stream idle. "No running Ollama server detected. Start it with `ollama serve`." Pulls the default model if it is missing. |
| Crush | provider `type` of `llamacpp`, `lmstudio`, `ollama`; an empty model list is filled from the server | The one with real discovery. |
| Zed | `auto_discover`, per-model `max_tokens`, `supports_tools`, `supports_thinking` | Discover by default, pin by hand. |
| Aider | `OLLAMA_API_BASE`; per-request `num_ctx = tokens × 1.25 + 8192` | Only possible on Ollama's native API. Strips `<think>` with `reasoning_tag`. |
| Goose | `GOOSE_TOOLSHIM`: a second small model turns text into tool calls | For models with no native tool calling. Open bugs against it. |
| Cline, Roo | Moved from XML-in-the-prompt to native tool calling, text path kept as fallback | Roo reads the Modelfile's `num_ctx`. |
| Continue | `capabilities: [tool_use]`; agent mode is unavailable without it | Capabilities can be added, not removed. |
| Qwen Code | `modelProviders` with `contextWindowSize` and `streamIdleTimeoutMs` | The third harness with a separate stream-idle timeout "for slow local servers". |

What Relay took: discovery instead of hand-declared windows, the short-probe and long-first-token
split, the actionable "not running" error, forced usage, client-side reasoning tags, and recovery
of text tool calls as an opt-in. What it left: Aider's per-request `num_ctx` (needs Ollama's native
`/api/chat`, a second transport), an interpreter model like Goose's, and servers on the LAN
(unencrypted traffic off this machine is the owner's decision). All three are deferred tasks on
the card.
