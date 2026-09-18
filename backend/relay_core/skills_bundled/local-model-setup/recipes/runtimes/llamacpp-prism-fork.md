# The PrismML llama.cpp fork (Bonsai models only)

verified: 2026-09-18. Repo confirmed; the build below is the one that was run on this machine
(a DGX Spark, GB10, aarch64) on 2026-09-18 and is serving `bonsai-2-27b` today.

Sources: <https://github.com/PrismML-Eng/llama.cpp> (README: "the PrismML fork of llama.cpp, the
main line behind the Bonsai models (branch `prism`)") · build guide inherited from upstream at
`/blob/prism/docs/build.md` · this repo's `docs/LOCAL-MODELS.md` and
`docs/qa_evidence/2026-09-18-local-models/`.

**Only for Bonsai files.** A Bonsai GGUF does not run on mainline llama.cpp: the ternary/Hadamard
kernels exist only here. And the failure is not a refusal — **a Bonsai file on the wrong build
loads and writes fluent nonsense**, which is exactly why phase 4(a) exists. Do not use this fork
for anything else; use upstream.

## Build

```sh
git clone https://github.com/PrismML-Eng/llama.cpp -b prism ~/data/llms/runtimes/llamacpp-prism
cd ~/data/llms/runtimes/llamacpp-prism
git rev-parse HEAD                  # record it in the README you write in phase 6
PATH=/usr/local/cuda/bin:$PATH cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DGGML_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=<cc>
cmake --build build -j $(nproc)
```

Known-good on this machine: commit `1a07bfa5f4144274c8f1c9963821dd9d9a51854b` (build `b10706`),
`-DCMAKE_CUDA_ARCHITECTURES=121` for GB10 (compute capability 12.1; the fork rewrites 121 → 121a).
No prebuilt binaries for aarch64 CUDA exist for this fork — building is the only route. Expect a
long build; say so before starting.

## Serve

Same flags as upstream (`recipes/runtimes/llamacpp.md`). What was used here:

```sh
build/bin/llama-server -m models/bonsai-2-27b/Ternary-Bonsai-2-27B-PQ2_0.gguf \
  --host 127.0.0.1 --port 8080 --alias bonsai-2-27b \
  -c 131072 -np 1 --jinja --reasoning-format deepseek --sleep-idle-seconds 600
```

Measured on this machine: ~17 GiB resident awake, ~3 GiB asleep, 1.6 s to reload from sleep,
~24 tok/s decode, ~660 tok/s prompt.

## Gates

`scripts/relay-local.py smoke http://127.0.0.1:8080 --model bonsai-2-27b` on 2026-09-18 passed
checks 1, 2, 3 and 5 and **failed check 4**: asked to explain the literal strings `<tool_call>` and
`</think>` in prose, the server's parser turned them into a tool call and the answer never arrived.
Usable as an agent model; tell the user it will stumble on text that contains those tags.
