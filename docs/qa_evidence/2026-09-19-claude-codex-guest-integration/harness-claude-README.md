# Tier A, Claude half: `claude -p --input-format stream-json` behind the harness contract

Implementer evidence for `backend/relay_core/guest_harness_claude.py` (GT7X, protocol 29, contract
`backend/relay_core/guest_harness.py`).

- **Claude Code:** `2.1.278 (Claude Code)`, `/home/elliott/.npm-global/bin/claude` (an
  aarch64 ELF binary, not a node script), signed in on a Claude Max subscription — so a turn
  spends the owner's plan, not dollars.
- **Machine:** the DGX Spark, Ubuntu 24.04 aarch64.
- **Real turns spent recording: nine prompts, eight of which reached a model**, all on
  `--model haiku` (Haiku 4.5) with the smallest prompts the task allows. The ninth was
  `/compact`, which the CLI refused for free. The six recordings that were kept report
  `total_cost_usd` of **$0.073 of list-price equivalent** between them; two further turns went on
  a false start that had to be re-recorded (see
  [The two wasted turns](#the-two-wasted-turns)). Everything else below — the whole control
  protocol, `set_model`, `interrupt`, which subtypes exist at all — was learned from **control
  requests, which cost nothing**: the CLI answers them without reaching a model.
- **Tests:** `PYTHONPATH=backend python3 -m unittest tests.test_guest_harness_claude` — **49
  tests, 0.04 s, no `claude` process started.** They replay the fixtures through a fake process
  object. `ctest`'s `backend-and-bash` picks them up by discovery; no `CMakeLists.txt` change was
  needed.

## How the protocol was learned

Claude Code's stream-json control protocol is not documented (a search of the public docs turns up
only third-party reverse engineering, and some of it is wrong — e.g. that `system`/`init` is
printed at startup, which it is not). None of what follows is from that search: every line is
from the installed CLI, either from `claude --help` or from a probe that sent a control request
and read the answer.

The one free instrument is the `initialize` control request. It is answered immediately, with no
model call, and its answer is a full description of the session:

```json
{"type":"control_request","request_id":"init-1","request":{"subtype":"initialize"}}
```
```json
{"type":"control_response","response":{"subtype":"success","request_id":"init-1","response":{
  "commands":[…78 slash commands…],"agents":[…],"output_style":"default",
  "models":[{"value":"default","resolvedModel":"claude-opus-5[1m]",…},{"value":"haiku",…}],
  "account":{"email":"…","organization":"…","subscriptionType":"Claude Max",
             "apiProvider":"firstParty"},
  "pid":233982,"current_permission_mode":"default","session_state":"idle",
  "pending_permission_requests":[],"pending_user_dialog_requests":[]}}}
```

Sending subtypes at an idle process is how the rest was mapped, still without a model call:

| control request | 2.1.278's answer |
| --- | --- |
| `{"subtype":"initialize"}` | success, the object above |
| `{"subtype":"set_model","model":"sonnet"}` | success, `response: null` |
| `{"subtype":"set_permission_mode","mode":"default"}` | success, `{"mode":"default"}`, plus a `system`/`status` carrying `permissionMode` |
| `{"subtype":"interrupt"}` | success, `{"still_queued":[]}` — answered even with nothing running |
| `{"subtype":"mcp_message"}` | success, `null` |
| `compact`, `compact_conversation`, `set_effort`, `can_use_tool`, `hook_callback`, `control_cancel_request`, `status`, `set_session_name` | `{"subtype":"error","error":"Unsupported control request subtype: <name>"}` |

That last row is the useful one twice over: there is **no compaction control request**, and an
unknown subtype fails loudly and identically, which is exactly the signal `set_model()` uses to
decide it is talking to an older CLI and must restart instead.

## The command line the adapter uses

```
claude -p --input-format stream-json --output-format stream-json --verbose
       --include-partial-messages
       [--model <M>]
       [--session-id <uuid4>  |  --resume <id> [--fork-session]]
       [--permission-mode bypassPermissions --dangerously-skip-permissions]   # permissions=bypass
       [--permission-prompts host --permission-prompt-tool stdio]             # permissions=ask
       [--permission-prompts none]                                            # permissions=deny
       [--settings <file>]
```

run in the pane's `cwd`, with `CLAUDECODE`, `CLAUDE_EFFORT` and every `CLAUDE_CODE_*` variable
removed from the child's environment (`guest_harness_claude.child_environment`). Relay may itself
be launched from inside a Claude Code session, and a child that inherits those inherits the parent
session's posture with them.

`--permission-prompt-tool stdio` is the one flag that had to be found by experiment; `--help`
mentions it only in passing, inside the description of `--permission-prompts`:

```
--permission-prompts <target>  Who answers permission prompts with --print: "host" (the SDK
                               host or --permission-prompt-tool) or "none" …
```

**`--permission-prompts host` on its own is not enough** — see
[The two wasted turns](#the-two-wasted-turns).

## The recorded flow, with the lines that matter

All six fixtures are one recording each, redacted; every line is quoted below from the fixture as
shipped. Direction markers: `in` is what the adapter writes, `out` what the CLI wrote.

### 1. Start: nothing is printed until we speak

`start()` spawns the process and sends `initialize`. The CLI's first output is whatever the
user's `SessionStart` hooks produce (`system`/`hook_started`, `system`/`hook_response` — the
adapter ignores both) and then the initialize answer. **There is no `system`/`init` yet.** The
init message — the one carrying `session_id`, `model` and `tools` — arrives only after the first
user message:

```
in   {"type":"control_request","request_id":"…","request":{"subtype":"initialize"}}
out  {"type":"control_response","response":{"subtype":"success",…}}
in   {"type":"user","message":{"role":"user","content":[{"type":"text","text":"Reply with the single word ok."}]}}
out  {"type":"system","subtype":"init","cwd":"…","session_id":"…","tools":["Task","Bash",…],
      "model":"claude-haiku-4-5-20251001","permissionMode":"bypassPermissions",…}
```

So `start()` **cannot** wait for `init`, and it does not. It returns a `HarnessStart` naming the
session id the adapter itself chose (`--session-id <uuid4>`, or the `resume` id), and the model
the caller asked for. The `started` event is emitted from the first `init` of the first turn,
which is also where `session_id`/`model` are corrected to what the CLI says. A **forked** session
is the one case where `start()` has no id to give (`--fork-session` makes a new one the CLI names,
not us), so `session_id` is `""` until the first turn's `init`.

### 2. A turn: deltas, then the answer, then `result`

```
out  {"type":"stream_event","event":{"type":"content_block_delta","index":0,
      "delta":{"type":"thinking_delta","thinking":"","estimated_tokens":50}}}
out  {"type":"stream_event","event":{"type":"content_block_delta","index":1,
      "delta":{"type":"text_delta","text":"ok"}}}
out  {"type":"assistant","message":{…,"content":[{"type":"text","text":"ok"}],…}}
out  {"type":"result","subtype":"success","is_error":false,"result":"…","num_turns":1,…}
```

`text_delta` → `delta`, `thinking_delta` → `thinking`, `signature_delta` → dropped. The whole
`assistant` message repeats what the deltas already said, so it is **not** re-emitted — except
when a turn produced no text delta at all, in which case the block goes out once as a `delta` so
the pane is never left blank (a guard against `--include-partial-messages` ever being unavailable).

### 3. A tool call

```
out  {"type":"assistant","message":{…,"content":[{"type":"tool_use",
      "id":"toolu_011xgkt7KwB25Y9LS47i1eNS","name":"Bash",
      "input":{"command":"echo relay-harness-ok","description":"Output a test string"},
      "caller":{"type":"direct"}}]}}
out  {"type":"user","message":{"role":"user","content":[{"tool_use_id":"toolu_011xgkt7…",
      "type":"tool_result","content":"relay-harness-ok","is_error":false}]},
      "tool_use_result":{"stdout":"relay-harness-ok","stderr":"","interrupted":false,…}}
```

→ `tool_started {call_id: "toolu_011xgkt7…", tool: "run_command", input: {command, description,
_guest_tool: "Bash"}, label: "Output a test string"}`, then `tool_result {call_id: …, tool:
"run_command", output: "relay-harness-ok", ok: true, ms: …}`.

`label` is the tool input's own `description` when it has one — Claude Code writes one for Bash,
and it is a better call line than anything Relay could rebuild.

### 4. An edit's diff comes off the wire

The `tool_use_result` beside a `tool_result` is where Edit/Write results keep their file state:

```
out  "tool_use_result":{"type":"create","filePath":"…/harness-ok.txt","content":"ok",
                        "structuredPatch":[],"originalFile":null,"userModified":false}
```

`structuredPatch` is a list of `{oldStart, oldLines, newStart, newLines, lines}` hunks for an
update, and empty for a create. The adapter prefers it, falls back to
`originalFile` → `content`, and only then to rebuilding a diff from the tool's own input
(`old_string`/`new_string` for Edit, the whole file for Write, each edit in turn for MultiEdit).
`diff` is omitted entirely when none of those exist, as the contract allows.

### 5. An approval, and answering it

With `--permission-prompt-tool stdio` the CLI asks us, on stdout, and waits:

```
out  {"type":"control_request","request_id":"13681634-…","request":{
      "subtype":"can_use_tool","tool_name":"Write","display_name":"Write",
      "input":{"file_path":"…/harness-ok.txt","content":"ok"},
      "description":"harness-ok.txt",
      "permission_suggestions":[{"type":"setMode","mode":"acceptEdits","destination":"session"}],
      "tool_use_id":"toolu_01Qk6bskAALZPCgLwUf7EHZn"}}
in   {"type":"control_response","response":{"subtype":"success","request_id":"13681634-…",
      "response":{"behavior":"allow","updatedInput":{"file_path":"…","content":"ok"}}}}
```

→ `approval {id: "13681634-…", kind: "patch", detail: "Write: …/harness-ok.txt"}`, and
`answer(id, {"behavior": "allow"})` writes the line above (`updatedInput` defaults to the input
the CLI asked with). `kind` is `command` for Bash/BashOutput/KillShell, `patch` for
Edit/Write/MultiEdit/NotebookEdit, `tool` otherwise.

The send loop is **not** blocked while an approval is outstanding: the request is what waits, not
the reader, so a second tool call in the same turn still streams. Any approval still unanswered
when the turn ends (or when `interrupt()` is called) is denied, so the CLI is never left holding
one.

### 6. An interrupt

```
in   {"type":"control_request","request_id":"int-1","request":{"subtype":"interrupt"}}
out  {"type":"control_response","response":{"subtype":"success","request_id":"int-1",
      "response":{"still_queued":[]}}}
out  {"type":"user","message":{…"tool_result","content":"The user doesn't want to proceed with
      this tool use. …"}}
out  {"type":"result","subtype":"error_during_execution","is_error":true,
      "terminal_reason":"aborted_tools","stop_reason":"tool_use","num_turns":3,
      "errors":["[ede_diagnostic] result_type=user last_content_type=n/a stop_reason=tool_use"]}
```

Two things to note, both load-bearing:

- **An interrupted turn reports `is_error: true` and has no `result` field at all.** "`is_error`
  → HarnessError" would turn every interrupt into an error, so the adapter checks
  `terminal_reason` (`aborted_tools` when a tool was cut off, `aborted_streaming` when text was)
  and its own "I asked for this" flag first. `TurnResult.text` then falls back to the assistant
  text collected so far.
- **The process survives it.** `interrupt-then-turn.jsonl` is one recording of an interrupted
  turn followed by `"Reply with the single word ok."` on the same process: `result` `success`,
  `"ok"`. No restart, no `--resume`. The fallbacks the task allowed for (SIGINT, or closing stdin
  and resuming) are not needed and are not implemented.

### 7. `/compact`

There is no control request for compaction, but a slash command **does** work as an ordinary user
message on this stream:

```
in   {"type":"user","message":{"role":"user","content":[{"type":"text","text":"/compact"}]}}
out  {"type":"system","subtype":"status","status":"compacting",…}
out  {"type":"system","subtype":"status","status":null,"compact_result":"failed",
      "compact_error":"Not enough messages to compact.",…}
out  {"type":"result","is_error":false,"num_turns":0,"total_cost_usd":0,…}
```

(The recording is of a fresh session, which is why it refuses; the mechanism is what is being
shown, and it cost nothing.) Compaction runs as a turn of its own and ends with a `result` of its
own, so `compact()` hands the work to a thread that takes the turn lock, writes the line and
swallows that `result` — otherwise the next `send()` would return the compaction's result as its
answer. `compact()` itself returns at once, as the contract requires.

## What worked

- Everything the card needs, on the first try, with no TUI and no scraping.
- `-p` skips the workspace-trust dialog and `--dangerously-skip-permissions` needs no first-run
  acceptance, exactly as the colleague's note said. A fresh `/tmp` working directory was used for
  every recording and neither ever appeared.
- `--session-id <uuid4>` means the pane knows the guest's session id **before** the first turn,
  which is what `guest_sessions` files a transcript under.
- `set_model` and `interrupt` are real control requests in 2.1.278; neither needs a restart.
- `result.modelUsage["claude-haiku-4-5-20251001"].contextWindow` is `200000`, so `context_pct`
  can be derived after all (see below).
- Non-JSON on stdout never actually happened in eight recordings — but the adapter skips it, and
  a test feeds it a stray `npm notice:` line and a bare JSON array to prove the turn still
  completes.

## What did not, or is not what the task assumed

1. **`system`/`init` is not emitted at startup.** It arrives after the first user message. The
   task anticipated this ("you may need to send the first prompt to get init"); what it means in
   practice is that `start()`'s liveness check is the `initialize` control request, and that
   `started` is a first-turn event. A dead process at that point is a `HarnessNotAvailable` with
   the stderr tail; a *silent* one (an older CLI that does not answer `initialize`) is allowed
   through with a debug line rather than failed, since nothing says it will not work.
2. **`--permission-prompts host` alone does not route approvals to the host.** Without
   `--permission-prompt-tool stdio` the CLI answers its own prompt with a refusal — the recording
   shows `system`/`permission_denied` and a `tool_result` of "Claude requested permissions to
   write to …, but you haven't granted it yet", and the model gives up. With
   `--permission-prompt-tool stdio` the same prompt arrives as a `can_use_tool` control request.
   The value `stdio` is not in `--help`'s flag list at all.
3. **`set_model` does not name the model back** (`response: null`), so `set_model()` returns the
   name it was given. The real name is picked up from the next turn's `assistant` messages, which
   carry `message.model`, and `harness.model` is corrected then. This is the one place the
   adapter's return value is a promise rather than an observation.
4. **`context_pct` is derived, not reported.** The statusline's
   `context_window.used_percentage` is not on this stream (confirmed: no `result` field carries
   it). What is there is `modelUsage[<model>].contextWindow`, so the adapter computes
   `(input_tokens + cache_read + cache_creation) / contextWindow` — the size of the last
   request's prompt, which is the number a context chip wants. It is absent from the `usage`
   event when a `result` carries no `modelUsage` (a `/compact` turn, for instance).
5. **`total_cost_usd` on a Max plan is list-price equivalent, not money spent.** It is passed
   through as `cost_usd` unchanged; whether the pane should show it for a subscription guest is a
   product question, not this module's.
6. **The `AskUserQuestion` path is implemented but not recorded.** A `can_use_tool` for
   `AskUserQuestion` becomes a protocol-27 `question` event, and `answer(id, {"answers": […]})`
   replies `{"behavior": "deny", "message": "The user answered: …"}` — deny is the only way to
   get text back into the conversation, since an allow would let the CLI try to ask a human that
   is not there. Covered by a hand-written test, not by a fixture: provoking a real
   `AskUserQuestion` would have cost turns on a path the card does not need yet.

## The two wasted turns

Worth recording so nobody spends them again:

1. `"Run `echo relay-harness-ok` with Bash"` under `--permission-mode default` **does not
   prompt**. `echo` is auto-approved (the user's settings carry no allowlist — it is the CLI's own
   judgement), so the recording came back with no `can_use_tool` at all. A tool that really
   prompts is needed: `Write` to a new file.
2. That second attempt then hit the `--permission-prompts host` gap in (2) above and was
   auto-denied. The third attempt, with `--permission-prompt-tool stdio`, is the fixture.

## Open questions

- **Does `set_model` take effect for the turn after it, and what does the CLI do with an
  unknown name?** Both would cost turns to answer, and neither changes the adapter's shape.
  `harness.model` self-corrects from the next turn's messages either way.
- **`permissions="deny"`** (`--permission-prompts none`) is built from `--help` and never
  recorded; nothing in the card needs it yet.
- **Images.** The user message carries `{"type":"image","source":{"type":"base64",…}}` blocks as
  the task specified; the shape is asserted by a test but has not been put in front of a real
  model.
- **The `--settings` file.** `guest_launch.claude_settings()` already builds the hook and
  statusline entries for the Tier B launch. Whether a Tier A harness should carry them too — the
  statusline chip is redundant when `usage` events exist, the hooks are how the pane learns about
  a guest it is *not* driving — is the owner's call; `ClaudeHarness(settings=…)` takes a path and
  passes it through, and passes nothing when given nothing.

## The fixtures

| file | what it is | lines |
| --- | --- | --- |
| `hello.jsonl` | handshake + "Reply with the single word ok." | 26 |
| `bash-tool.jsonl` | handshake + a turn that runs `echo relay-harness-ok` | 50 |
| `approval-write.jsonl` | handshake + a `Write` approved over the wire (`permissions="ask"`) | 60 |
| `interrupt.jsonl` | handshake + a turn interrupted at its first tool call | 37 |
| `interrupt-then-turn.jsonl` | the same, then a second turn on the same process | 70 |
| `compact.jsonl` | handshake + `/compact` as a user message | 12 |

Every line is `{"dir": …, …}`: `meta` (the command line it was recorded with, first and last),
`in` (a line the adapter is expected to write), `out` (a line the CLI wrote), `out_raw` (a line
that was not JSON), `err` (stderr). `FakeClaude` in the test file replays one by waiting for each
`in` line before going on, so a fixture is a two-sided script: an adapter that stops writing what
the CLI was answering hangs on that line rather than passing.

Redacted, structure kept: `/home/elliott` → `/home/user`, the account object → a placeholder,
every UUID → a stable UUID5 of itself (so cross-references still line up), `msg_…`/`req_…` ids →
`msg_REDACTED`/`req_REDACTED`, thinking `signature` blobs → `"REDACTED-SIGNATURE"`,
`total_cost_usd`/`costUSD` → `0.0`, rate-limit `utilization` → `0.0`, `pid` → `0`. Two long lists
in the `initialize` answer — `commands` (78 entries naming the owner's installed plugins and
skills) and `agents` — are trimmed to the first entries plus a `{"_trimmed": n}` marker, and
`slash_commands` to the first five; nothing in the adapter reads either. Token counts are kept as
recorded, because the `context_pct` test does arithmetic on them.

The recorder and the redactor were throwaway scripts under `/tmp/claude-1000/hclaude/`; the
fixtures are the artefact, and the test file documents their format.
