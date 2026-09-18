# Servers Relay can use but does not install

verified: 2026-09-18, ports and start commands from each project's own docs.

If one of these is already running, **that is the cheapest possible answer**: nothing to download,
nothing to install. Do not install any of them as part of this skill — if none is running and the
user has no preference, use `ollama.md` or `llamacpp.md`.

For every one of them the route is the same:

```sh
scripts/relay-local.py probe http://127.0.0.1:<port>          # what is there, and its window
scripts/relay-local.py smoke http://127.0.0.1:<port> --model <id>
scripts/relay-local.py add --base-url http://127.0.0.1:<port> --id <slug> --label "<name>" --detect
```

| Server | Port | Starts with | OpenAI base | Notes |
|---|---|---|---|---|
| **LM Studio** | 1234 | `lms server start` (or Developer › Start Server) | `/v1` | `--port N` overrides; otherwise it reuses the last port. Relay detects it from the model rows' `loaded_context_length`. |
| **SGLang** | 30000 | `python3 -m sglang.launch_server --model-path <m> --port 30000` | `/v1` | Docs moved to docs.sglang.io. Bind to 127.0.0.1. |
| **Jan** | 1337 | GUI only: Settings › Local API Server › Start Server | `/v1` | No documented CLI start command. Say so rather than inventing one. |
| **llamafile** | 8080 | `./Model.llamafile --server --tools all` | `/v1` | Docs now at docs.mozilla.ai/llamafile. Probes as llama.cpp, since it is one. |
| **KoboldCpp** | 5001 | `python koboldcpp.py <model.gguf> [port]` | `/v1` | Its own API is at `/api`; the OpenAI one is at `/v1`. |
| **Lemonade** | 13305 | the daemon is `lemond`; the client CLI is `lemonade run/list` | `/v1` **or** `/api/v1` — the official pages disagree | Probe both bases before registering. |
| **Docker Model Runner** | 12434 | `docker desktop enable model-runner --tcp <port>` | **`/engines/v1`**, not `/v1` | Register the full base: `http://127.0.0.1:12434/engines/v1`. |
| **ds4** (antirez) | 8000 | `./ds4-server --ctx 32768` | `/v1` | "DeepSeek 4 Flash and PRO local inference engine for Metal, CUDA and ROCm". Self-described beta: "The software is currently very fast changing." Reads GGUF, but only the model families the project ships. |

Two things Relay's probe cannot guess:

- **The window.** A server that does not report one gets 32,768. If the user started it with a
  different context, pass `--context-window N` to `add`.
- **Whether the template takes tools.** `--detect` fills `tools` only when the server says. The
  smoke test is what actually answers the question.
