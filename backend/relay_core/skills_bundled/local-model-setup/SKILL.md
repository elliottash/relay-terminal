---
name: local-model-setup
description: Set up a local model server (llama.cpp, Ollama, vLLM) on this machine and register it with Relay: survey, choose, serve, smoke-test, register.
short: Run a model locally (llama.cpp, Ollama, vLLM) and register it with Relay.
---

# Set up a local model for Relay

You are doing this *for* the user, on their machine, with them watching. Work in phases. **End every
phase with a short report and, where it says so, wait for the user's answer.** Never run a phase's
commands before the phase before it has passed.

**Find Relay's own scripts once, first.** Everything below says `scripts/relay-local.py`; that is
relative to a Relay checkout. If the working directory is not one, look in
`/usr/share/relay/scripts/`, `/usr/local/share/relay/scripts/` and `~/repos/relay-terminal/scripts/`,
or ask the user where Relay is, and use the path you found from then on.

Read on demand, with `read_skill_file`, not up front:

- `recipes/runtimes/ollama.md` — easiest; a background service that manages models for you.
- `recipes/runtimes/llamacpp.md` — most control; one server per model, idle unload.
- `recipes/runtimes/llamacpp-prism-fork.md` — only for Bonsai models; they run on nothing else.
- `recipes/runtimes/vllm.md` — big NVIDIA GPUs, throughput, no idle unload.
- `recipes/runtimes/detect-only.md` — LM Studio, SGLang, Jan, llamafile, KoboldCpp, Lemonade,
  Docker Model Runner, ds4: no install recipe, just how to register one that is already running.
- `recipes/models.json` — the candidate models, by memory tier, with sizes, licences and flags.

**The catalog ages fast.** Every entry carries a `verified` date and a `status`. Before you propose
a model, check its live listing (its Ollama library page or its Hugging Face repo) and say so. Only
two entries were run on this machine through Relay; everything else is a listing, not a promise.
**The smoke test decides, not the catalog.**

## Phase 1 — Survey (read-only)

Run these and nothing else. Do not install, do not download, do not test `sudo`.

```sh
uname -srm; cat /etc/os-release | head -2      # aarch64 vs x86_64 decides which binaries exist
nvidia-smi --query-gpu=name,compute_cap,driver_version,memory.total --format=csv   # NVIDIA
rocm-smi 2>/dev/null | head -20                                                    # AMD
free -h; df -h ~ /tmp /var                     # RAM and free disk
command -v ollama llama-server llama lms vllm docker
ollama ps; ollama ls                           # if ollama is there: what is loaded, what is pulled
ls -lh ~/*.gguf ~/models ~/.cache/huggingface/hub 2>/dev/null | head -30
scripts/relay-local.py scan                    # what is already serving on 11434/1234/8080/8000
```

Notes that change the answer:

- **Unified memory** (DGX Spark / GB10, Apple Silicon, Strix Halo): `nvidia-smi` reports
  `memory.total [N/A]`. Use `free -h`; the GPU budget is system RAM. Leave 40–60 GiB for the
  desktop on a 128 GiB machine.
- **Compute capability** is what a CUDA build needs (`nvidia-smi` prints e.g. `12.1` → `121`).
- **aarch64** rules out most prebuilt binaries and most one-line installers. Check the recipe.
- If `scripts/relay-local.py scan` finds a server already answering, that is very likely the
  answer: skip to phase 4.

Report: machine, accelerator, memory, free disk, what is installed, what is already serving.

## Phase 2 — Propose, and let the user choose

Offer **2–3 options that fit this machine**, from `recipes/models.json` filtered by the memory tier
you measured. For each: name, what it is good at, download size, memory held, expected speed if the
catalog knows it, licence, runtime, and **what has to be installed**.

Prefer, in this order: a server already running > a model already pulled in Ollama > a runtime
already installed > a new install. Say plainly when an option costs nothing.

**The user chooses. Do not pick for them.** Then, before downloading:

- over **5 GB**: confirm.
- over **20 GB**: explicit confirmation, with the size in the question.
- over **60 GB free disk** needed: check `df -h` first and say the numbers.

## Phase 3 — Install and serve

Follow the runtime recipe. These rules override anything a recipe implies:

- **Loopback only.** `--host 127.0.0.1`. Relay talks to nothing else, and nothing else should
  reach the server.
- **No sudo without asking.** Explain exactly what needs root and why, then wait. If the user says
  no, use an option that does not need it.
- **Never touch a service the user already has.** Do not restart, reconfigure, `systemctl edit` or
  stop an existing Ollama/Docker/anything without asking first, even to change one variable.
- **No `curl | sh` from a domain the recipe does not name**, and only after the user says yes.
- **Pin what you build.** Record the git commit, the branch and the build flags.
- **Logs to a file**, under the model directory.
- **A systemd *user* unit** (`~/.config/systemd/user/…`), started by hand:
  `systemctl --user start <name>`. **Do not `enable` it** unless the user asks for boot start.
- **Idle unload on**: llama.cpp `--sleep-idle-seconds 600`; Ollama `keep_alive` / `OLLAMA_KEEP_ALIVE`.
  A model that holds 20 GiB forever is a machine the user cannot work on.

Report: what was installed, where, the version or commit, and how to start and stop it.

## Phase 4 — The gates, in order. Stop at the first failure.

**(a) One sentence.** A wrong quant, a wrong build or a broken template does not fail — it writes
fluent nonsense. Ask for one sentence on a concrete subject and *read the answer yourself*:

```sh
curl -s http://127.0.0.1:<port>/v1/chat/completions -H 'Content-Type: application/json' \
  -d '{"model":"<id>","messages":[{"role":"user","content":"Write one sentence about the sea."}]}' \
  | python3 -c 'import json,sys; print(json.load(sys.stdin)["choices"][0]["message"])'
```

If it is not a coherent English sentence about the sea, the build and the file disagree. Stop, say
so, and go back to phase 3.

**(b) The tool-call smoke test.** This is the one that matters:

```sh
scripts/relay-local.py smoke http://127.0.0.1:<port> --model <id>
```

Five checks: it answers; a native tool call with valid JSON arguments; **two** consecutive tool
calls in one conversation then a plain answer; the literal strings `<tool_call>` and `</think>`
survive inside prose; token usage arrives and is plausible. Each failure prints what to do. Three
common ones:

- *tool call arrived as text* → llama.cpp: add `--jinja` (and `--chat-template-file` if the GGUF's
  own template is wrong). If the server cannot be fixed, `add --tool-text-recovery` is the fallback,
  and it is off by default for a reason: a parser that misfires runs a command out of an answer.
- *the second call fails* → the template is broken past the first turn. A different quant or a
  different model; this one will die halfway through real work.
- *the tags were parsed as a tool call* → the server's parser fires on text a user can type. Usable,
  but say so: Bonsai 2 on the PrismML fork fails exactly this check today.

**(c) Ollama only:** `ollama ps` and read the **CONTEXT** column. If it is not the window you
intended, the endpoint will silently be small. See the recipe (`PARAMETER num_ctx` + `ollama create`
is the fix that touches no service).

Report each gate with its result, and never round a failure up to a pass. A model that cannot make
two consecutive tool calls is not set up, whatever the download said; one that only fails the tags
check can be registered, with that sentence said out loud.

## Phase 5 — Register it, then prove it

```sh
scripts/relay-local.py add --base-url http://127.0.0.1:<port> --id <slug> --label "<name>" --detect
```

Add whatever the catalog's `flags` say this model needs (for example
`--tool-arguments-as-object`, `--temperature`, `--first-token-timeout`). `--detect` fills the model
id, the server kind and the **served** window — so if the server is ever restarted with a different
`-c`, run it again.

Then prove the whole path, agent included:

```sh
scripts/relay-agent.py --provider local:<slug> --yes \
  --prompt "Run 'wc -l README.md' with run_command and tell me the number."
```

It must actually call the tool and answer with the real number.

Finally, tell the user:

- it is now a row in every pane's **model dropdown**, and can be a Main/Flash/Lite role in
  Settings › Models › Model roles;
- **start / stop**: `systemctl --user start|stop <unit>` (llama.cpp), or `ollama stop <model>` /
  the model loads on demand (Ollama);
- there is no API key and nothing leaves the machine.

## Phase 6 — Write it down

Create or extend a `README.md` **next to the models** (not in a git repo, not in `~`), holding:
model file and its source repo, runtime with version/commit and build flags, the launcher or unit
file with its path, the served id, port, context, measured memory and speed, start/stop/log
commands, and the date. Point the user at it and at `docs/LOCAL-MODELS.md`.

## If it goes wrong

- Nothing answers → `scripts/relay-local.py probe http://127.0.0.1:<port>` prints the command that
  would start it.
- 503 for a while after start is normal: the weights are loading. Relay waits it out.
- A sleeping llama-server is `"is_sleeping": true` on `/props`; the next request wakes it.
- Out of memory while loading → a smaller quant or a smaller `-c`, not a bigger swap file.
