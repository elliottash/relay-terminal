# llama.cpp (`llama-server` / `llama serve`)

verified: 2026-09-18 against the official repo only.

Sources: <https://github.com/ggml-org/llama.cpp> (README) ·
<https://raw.githubusercontent.com/ggml-org/llama.cpp/master/docs/build.md> ·
<https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md> ·
<https://raw.githubusercontent.com/ggml-org/llama.cpp/master/docs/function-calling.md> ·
<https://api.github.com/repos/ggml-org/llama.cpp/releases>

**Pick it when** the model is a GGUF, the window must be an exact number, or the machine should get
its memory back when nobody is using the model. One server per model; Relay talks to the port.

## Getting a binary — three routes, in this order

1. **Already there.** `command -v llama-server llama` — if either exists, use it and skip the rest.
   (Both spellings are current: the README's quick start uses `llama serve`, the server docs and
   older builds use `llama-server`. Try `llama serve --help`, fall back to `llama-server --help`.)
2. **Prebuilt binaries.** The GitHub **releases** page. Note: the `latest` release carries no
   binaries — they are on the nightly prereleases tagged `b#####`, with names like
   `llama-b11042-bin-ubuntu-cuda-13.3-arm64.tar.gz`, `…-cuda-13.3-x64.tar.gz`,
   `…-bin-ubuntu-arm64.tar.gz`, `…-bin-ubuntu-rocm-10.0-x64.tar.gz`, `…-bin-macos-arm64.tar.gz`.
   Resolve the newest `b#####` tag at runtime; never hardcode one. A CUDA build also needs the
   matching `cudart-…` archive.
3. **Build from source** (below). Always the answer for an unusual compute capability.

The official README's quick start says "Visit https://llama.app and follow the instructions"
(checked 2026-09-18), and that site's installer is `curl -LsSf https://llama.app/install.sh | sh`.
The README names the domain, not the command, and it is a piped script: show the user the
command, ask first, and only run it if they say yes. Prefer options 1 to 3. Never substitute any
other domain.

## Build from source (CUDA)

```sh
git clone https://github.com/ggml-org/llama.cpp && cd llama.cpp
git rev-parse HEAD                      # record this: the build is only reproducible with it
cmake -B build -DGGML_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=<cc>   # cc from nvidia-smi: 12.1 -> 121
cmake --build build --config Release -j $(nproc)
```

`-DGGML_CUDA=ON` and `-DCMAKE_CUDA_ARCHITECTURES` are both in the official build guide (its example
is `-DCMAKE_CUDA_ARCHITECTURES="86;89"`). Metal on macOS and `-DGGML_HIP=ON` for ROCm are in the
same guide. A CUDA build takes a long time; tell the user before starting it.

The binaries land in `build/bin/`.

## Serve

```sh
llama-server -m /path/model.gguf \
  --host 127.0.0.1 --port 8080 --alias my-model \
  -c 65536 -np 1 -ngl 99 -fa on \
  --jinja --reasoning-format deepseek \
  --sleep-idle-seconds 600 \
  > ~/data/llms/logs/my-model.log 2>&1
```

`-hf <user>/<repo>[:QUANT]` downloads instead of `-m` (quant is optional and case-insensitive,
default `Q4_K_M`, else the first file in the repo).

| Flag | Official description, and why it matters |
|---|---|
| `--jinja` | jinja template engine for chat; **tool use requires it**. Without it, calls arrive as text. |
| `--chat-template-file F` | custom jinja template; the fix when the GGUF's own template is wrong. |
| `--reasoning-format` | `none`, `deepseek`, `deepseek-legacy` (default `auto`). `deepseek` puts thinking in `reasoning_content`, out of the answer. |
| `-c, --ctx-size N` | prompt context (default 0 = from the model). 32K is the floor for tool use; 64K+ is comfortable. |
| `-np, --parallel N` | server slots. `1` gives the whole window to one conversation. |
| `-a, --alias NAME` | the model id the API reports — a stable name instead of a file path. |
| `--host` / `--port` | defaults are `127.0.0.1` and `8080`. Keep the host. |
| `-ngl N` | layers in VRAM (default `auto`; `99`/`all` for "everything"). |
| `-fa on\|off\|auto` | flash attention. |
| `--sleep-idle-seconds N` | sleep after N idle seconds (default `-1`, disabled). Frees the weights; the next request reloads them. |
| **not** `-ctk q4_0` | the official docs: "Beware of extreme KV quantizations (e.g. `-ctk q4_0`), they can substantially degrade the model's tool calling performance." |

## A systemd user unit

`scripts/relay-local.py unit` prints a ready `llama-server@.service` (one instance per model, args
from `~/.config/relay/llama-server/<name>.env`) and installs nothing. Write it to
`~/.config/systemd/user/`, then:

```sh
systemctl --user daemon-reload
systemctl --user start llama-server@<name>      # do NOT `enable` unless the user asks
journalctl --user -u llama-server@<name> -f
```

## Checks

- `curl -s 127.0.0.1:8080/props` → `default_generation_settings.n_ctx` is the **served** window,
  `is_sleeping` says whether the weights are resident.
- `curl -s 127.0.0.1:8080/v1/models` → the id `--alias` gave it.
- The server log prints a `Chat format:` line naming the chat handler it chose. The format names
  differ between builds (an older doc's `Generic` meant "no native tool format"; current master
  prints names like `Content-only` and `peg-native`), so **do not decide anything from that line** —
  run `scripts/relay-local.py smoke` and read the checks.

## Register

```sh
scripts/relay-local.py add --base-url http://127.0.0.1:8080 --id <slug> --label "<name>" --detect
```

Re-run `--detect` after any restart with a different `-c`: Relay stores the window, it does not
re-read it every turn.
