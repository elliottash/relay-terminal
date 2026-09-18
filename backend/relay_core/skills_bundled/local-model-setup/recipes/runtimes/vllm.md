# vLLM

verified: 2026-09-18.

Sources: <https://docs.vllm.ai/en/latest/getting_started/quickstart.html> ·
<https://docs.vllm.ai/en/latest/features/tool_calling.html> ·
<https://docs.vllm.ai/en/latest/features/reasoning_outputs.html> ·
<https://docs.vllm.ai/en/latest/getting_started/installation/gpu.html> ·
<https://docs.vllm.ai/en/latest/features/sleep_mode/>

**Pick it only for** a large NVIDIA GPU with the whole model in VRAM, when throughput or several
callers at once matter. For one user at a desktop it is the wrong trade: it is a heavy install and
**it never frees the weights on its own** (see below). Prefer llama.cpp or Ollama.

## Install and serve

```sh
uv venv && uv pip install vllm          # or pip, in a venv
vllm serve <hf-repo> --host 127.0.0.1 --port 8000 \
    --enable-auto-tool-choice --tool-call-parser <family> --reasoning-parser <family>
```

`--port` defaults to 8000. `--enable-auto-tool-choice` is documented as **mandatory** for the model
to generate tool calls at all, and it needs the parser for the model's family — **without both,
tool calls never arrive**, whatever the model can do.

Parser names in the docs' own examples (the page has no flat list; check it for the model at hand):
`hermes`, `llama3_json`, `llama4_pythonic`, `mistral`, `granite`, `granite4`, `deepseek_v3`,
`deepseek_v31`, `qwen3_xml` (this is the Qwen3-Coder parser — **not** `qwen3_coder`), `kimi_k2`,
`glm45`, `glm47`, `openai`, `pythonic`. Reasoning parsers: `deepseek_r1`, `qwen3`, `glm45`,
`gemma4`, `granite`, and others on the reasoning page.

## aarch64

The claim "aarch64 is Docker-only" is **not what the docs say**. The GPU install page builds its
release-wheel URL from `CPU_ARCH=$(uname -m)  # x86_64 or aarch64`, so CUDA wheels exist for
aarch64; the Arm CPU page says prebuilt Arm wheels have shipped since 0.11.2. The *dedicated*
aarch64 GPU instructions are Docker-oriented (Grace-Hopper, `--platform linux/arm64`). Expect a
longer, rougher install on aarch64 either way, and say so before starting.

## No idle unload

There is no idle timeout. Sleep mode exists but is **manual and dev-only**: `--enable-sleep-mode`
plus `POST /sleep`, `POST /wake_up`, `GET /is_sleeping`, gated behind `VLLM_SERVER_DEV_MODE=1`, and
the docs say those endpoints should not be exposed to users. So a vLLM server holds its memory from
start to stop. Tell the user, and give them the stop command.

## Register

```sh
scripts/relay-local.py add --base-url http://127.0.0.1:8000 --id <slug> --label "<name>" --detect
```

The probe reads vLLM's `max_model_len` as the window.
