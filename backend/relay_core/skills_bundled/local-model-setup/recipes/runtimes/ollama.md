# Ollama

verified: 2026-09-18 (against docs.ollama.com and the install script itself; version seen here 0.34.0)

Sources: <https://docs.ollama.com/linux> · <https://docs.ollama.com/cli> ·
<https://docs.ollama.com/context-length> · <https://docs.ollama.com/faq> ·
<https://docs.ollama.com/api/openai-compatibility> ·
<https://raw.githubusercontent.com/ollama/ollama/main/scripts/install.sh>

**Pick it when** the user wants the least work, a model that is in the Ollama library is good
enough, and a background service that loads and unloads models by itself is acceptable.
**Avoid it when** the context window has to be exact and the user does not want their service
touched — read "Context" below before promising a window.

## Install

If `command -v ollama` already finds it, **use it. Do not reinstall and do not restart it.**

Otherwise, the official Linux install is a piped script **that uses sudo** (it writes
`/usr/local/bin/ollama`, creates the `ollama` user and installs
`/etc/systemd/system/ollama.service`, `ExecStart=… ollama serve`, `Restart=always`). Ask the user
first, in these words, and only run it if they say yes:

```sh
curl -fsSL https://ollama.com/install.sh | sh
```

Only `amd64` and `arm64` are handled by that script. The manual form, same page:

```sh
curl -fsSL https://ollama.com/download/ollama-linux-amd64.tar.zst | sudo tar x -C /usr
```

Not installed system-wide and the user refuses sudo → use llama.cpp instead.

## Serve and pull

The installer leaves the service running, so there is usually nothing to start. Otherwise
`ollama serve` runs it in the foreground on `127.0.0.1:11434`.

```sh
ollama pull <model>          # download
ollama ls                    # what is on disk (`ollama list` also works on 0.34)
ollama ps                    # what is loaded, its PROCESSOR and its CONTEXT
ollama stop <model>          # unload now
```

Base URL for Relay: `http://127.0.0.1:11434/v1`, model id exactly as `ollama ls` prints it
(including `:latest`).

## Context — read this before you promise a window

The `/v1` OpenAI endpoint **cannot set a context size per request**; the docs say so and give the
Modelfile as the answer. The default depends on VRAM (docs.ollama.com/context-length, verbatim):

| VRAM | default context |
|---|---|
| < 24 GiB | 4k |
| 24–48 GiB | 32k |
| ≥ 48 GiB | 256k |

The same page: "Tasks which require large context like web search, agents, and coding tools should
be set to at least 64000 tokens." Two ways to get one, in order of preference:

**1. A derived model (touches no service — prefer this).**

```sh
printf 'FROM <model>\nPARAMETER num_ctx 65536\n' > /tmp/Modelfile
ollama create <model>-64k -f /tmp/Modelfile
ollama run <model>-64k ""      # load it once
ollama ps                      # CONTEXT must now read 65536
```

Register `<model>-64k` with Relay, not the original.

**2. `OLLAMA_CONTEXT_LENGTH` (edits the user's service — ask first).**

```sh
OLLAMA_CONTEXT_LENGTH=64000 ollama serve     # foreground, no service change
```

For the installed service the documented route is `systemctl edit ollama.service`, add under
`[Service]` a line like `Environment="OLLAMA_CONTEXT_LENGTH=64000"`, then
`systemctl daemon-reload && systemctl restart ollama`. **That restarts their Ollama and changes it
for every client they have.** Do not do it without an explicit yes.

Always confirm with `ollama ps` and its CONTEXT column — that is the number the endpoint really
has. Ollama's docs do not state what happens when a conversation exceeds it, so do not tell the
user it is safe; register the real number so Relay compacts before it gets there.

## Idle unload

Models are kept in memory **5 minutes** by default, then unloaded. Per request the API takes
`"keep_alive": "10m"` (`0` unload at once, `-1` keep forever); `OLLAMA_KEEP_ALIVE` sets the global
default. Relay does not send `keep_alive`, so the default applies — which is the behaviour you
want. `ollama stop <model>` frees the memory immediately.

## What the `/v1` endpoint supports

`tools` yes; `tool_choice`, `n`, `logit_bias` and logprobs no. `stream_options.include_usage` yes,
which is what Relay's context tracker needs. Only pick a model whose `capabilities` include
`tools` (`ollama show <model>`); a model without them cannot drive the agent.

## Register

```sh
scripts/relay-local.py add --base-url http://127.0.0.1:11434 --id <slug> \
    --label "<name>" --model <model-as-listed> --detect
```

`--detect` reads the capabilities and the context length Ollama reports for that model. If the
smoke test shows tool arguments arriving as a JSON object rather than a string, add
`--tool-arguments-as-object`.
