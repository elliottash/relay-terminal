# Tier A, Codex half: `codex app-server` behind the harness contract

Implementer evidence for `backend/relay_core/guest_harness_codex.py` (GT7X, protocol 29, contract
`backend/relay_core/guest_harness.py`).

- **Codex:** `codex-cli 0.155.1`, `/home/elliott/.npm-global/bin/codex`, signed in with a ChatGPT
  Pro plan (so a turn costs plan quota, not dollars).
- **Machine:** the DGX Spark, Ubuntu 24.04 aarch64.
- **Real turns spent recording: four.** One "ok" turn, one shell-command turn, one interrupted
  turn (cut off at the first item, so it produced almost no output), one file-write turn under
  `approvalPolicy: on-request` to capture a real approval. Nothing else reached a model; the
  handshake and `model/list` recording cost nothing.
- **Tests:** `PYTHONPATH=backend python3 -m unittest tests.test_guest_harness_codex` — 35 tests,
  0.04 s, no codex process started. They replay the fixtures through a fake process object.

## How the protocol was learned

The app-server protocol is machine-readable, so none of it was guessed:

```
codex app-server generate-json-schema --out /tmp/claude-1000/hcodex/schema
```

writes 39 schema files plus `v1/` and `v2/` directories. The three that matter:

- `ClientRequest.json` — 100 request methods with their params (`initialize`, `thread/start`,
  `thread/resume`, `thread/fork`, `turn/start`, `turn/interrupt`, `thread/compact/start`,
  `model/list`, …).
- `ServerRequest.json` — the ten requests the server makes of *us*:
  `item/commandExecution/requestApproval`, `item/fileChange/requestApproval`,
  `item/tool/requestUserInput`, `item/permissions/requestApproval`,
  `mcpServer/elicitation/request`, `item/tool/call`, the two v1 names
  (`execCommandApproval`, `applyPatchApproval`), `attestation/generate`,
  `account/chatgptAuthTokens/refresh`.
- `ServerNotification.json` — 82 notifications, and `definitions.ThreadItem` with the 19 item
  types (`commandExecution`, `fileChange`, `mcpToolCall`, `dynamicToolCall`,
  `collabAgentToolCall`, `webSearch`, `imageView`, `agentMessage`, `reasoning`, …). Those item
  `type` strings are exactly what `guest_harness.map_tool_name("codex", …)` already keys on.

## The commands that were run

```
# 1. the protocol, free
codex app-server generate-json-schema --out /tmp/claude-1000/hcodex/schema

# 2. handshake + model/list, free (no model call)
python3 /tmp/claude-1000/hcodex/record.py <out>.jsonl <cwd> models

# 3. one turn each (the recorder drives codex app-server over stdio and logs both directions)
RELAY_PROMPT='Reply with the single word ok.'                     python3 record.py … normal
RELAY_PROMPT='Run `echo relay-harness-ok` and report its output.' python3 record.py … normal
RELAY_PROMPT='Count from 1 to 200, one number per line.'          python3 record.py … interrupt
RELAY_PROMPT='Create a file named ok.txt containing the word ok.' python3 record.py … approval

# 4. redact and save as fixtures
python3 redact.py <recording>.jsonl tests/fixtures/guest_harness_codex/<name>.jsonl
```

The recorder and the redactor are throwaway scripts under `/tmp/claude-1000/hcodex/`; the
fixtures they produced are the artefact, and the test file documents their format.

## The recorded flow, with the lines that matter

`tests/fixtures/guest_harness_codex/ok-turn.jsonl` (23 lines, `{"dir": "->" | "<-" | "<-raw",
"line": …}`):

```jsonc
-> {"jsonrpc":"2.0","id":1,"method":"initialize",
    "params":{"clientInfo":{"name":"relay","version":"0.1.0"}}}
<- {"id":1,"result":{"userAgent":"relay/0.155.1 …","codexHome":"…","platformOs":"linux"}}
<- {"method":"configWarning","params":{"summary":"Codex's Linux sandbox uses bubblewrap…"}}
-> {"jsonrpc":"2.0","method":"initialized","params":{}}
-> {"jsonrpc":"2.0","id":2,"method":"thread/start",
    "params":{"cwd":"…","approvalPolicy":"never","sandbox":"danger-full-access"}}
<- {"id":2,"result":{"thread":{"id":"01a0ba5c-89bb-…","sessionId":"01a0ba5c-89bb-…",
                               "path":"~/.codex/sessions/2026/09/19/rollout-….jsonl"},
                     "model":"gpt-5.6-sol","modelProvider":"openai",
                     "approvalPolicy":"never","sandbox":{"type":"dangerFullAccess"}}}
-> {"jsonrpc":"2.0","id":3,"method":"turn/start",
    "params":{"threadId":"01a0ba5c-89bb-…","input":[{"type":"text","text":"Reply with…"}]}}
<- {"id":3,"result":{"turn":{"id":"01a0ba5c-89f0-…","status":"inProgress"}}}
<- {"method":"turn/started",…}
<- {"method":"item/started","params":{"item":{"type":"userMessage",…}}}
<- {"method":"item/started","params":{"item":{"type":"agentMessage","id":"msg_…","text":"",
                                              "phase":"final_answer"}}}
<- {"method":"item/agentMessage/delta","params":{"itemId":"msg_…","delta":"ok"}}
<- {"method":"item/completed","params":{"item":{"type":"agentMessage","text":"ok",
                                                "phase":"final_answer"}}}
<- {"method":"thread/tokenUsage/updated","params":{"tokenUsage":{
     "total":{"totalTokens":13317,"inputTokens":13312,"outputTokens":5,…},
     "last":{"totalTokens":13317,…},"modelContextWindow":258400}}}
<- {"method":"turn/completed","params":{"turn":{"id":"01a0ba5c-89f0-…","status":"completed",
                                                "items":[{"type":"agentMessage","text":"ok"}]}}}
```

`shell-turn.jsonl` adds the tool half — note that codex narrates first with a `commentary`-phase
message and answers with a `final_answer` one:

```jsonc
<- {"method":"item/completed","params":{"item":{"type":"agentMessage","phase":"commentary",
     "text":"I’ll run that command and report the exact output."}}}
<- {"method":"item/started","params":{"item":{"type":"commandExecution",
     "id":"exec-fd850614-…","command":"/bin/bash -lc 'echo relay-harness-ok'",
     "cwd":"…","status":"inProgress","source":"unifiedExecStartup"}}}
<- {"method":"item/completed","params":{"item":{"type":"commandExecution","id":"exec-fd850614-…",
     "status":"completed","exitCode":0,"durationMs":0,
     "aggregatedOutput":"relay-harness-ok\n"}}}
<- {"method":"item/completed","params":{"item":{"type":"agentMessage","phase":"final_answer",
     "text":"`relay-harness-ok`"}}}
```

`approval-turn.jsonl` is the same run under `approvalPolicy: on-request`, `sandbox: read-only`:

```jsonc
<- {"method":"item/started","params":{"item":{"type":"fileChange","id":"exec-8ecc00d2-…",
     "status":"inProgress","changes":[{"path":"…/ok.txt","kind":{"type":"add"},"diff":"ok\n"}]}}}
<- {"id":0,"method":"item/fileChange/requestApproval",
    "params":{"threadId":"…","turnId":"…","itemId":"exec-8ecc00d2-…","reason":null}}
-> {"jsonrpc":"2.0","id":0,"result":{"decision":"accept"}}
<- {"method":"serverRequest/resolved","params":{"requestId":0}}
<- {"method":"item/completed","params":{"item":{"type":"fileChange","status":"completed",…}}}
```

`interrupt-turn.jsonl` stops at the first item:

```jsonc
-> {"jsonrpc":"2.0","id":4,"method":"turn/interrupt",
    "params":{"threadId":"…","turnId":"01a0ba5d-1661-…"}}
<- {"id":4,"result":{}}
<- {"method":"turn/completed","params":{"turn":{"status":"interrupted","items":[]}}}
```

## What worked

- Everything the card needs. `initialize` + `initialized` + `thread/start` is enough; the thread
  id doubles as `sessionId` and as the name `thread/resume` takes, so it is the harness's
  `session_id`.
- `turn/start` returns the turn id synchronously, so `interrupt()` never has to guess one — and
  the interrupt was answered and honoured within a second of being sent.
- The approval round trip: a server-to-client JSON-RPC **request** (`"id"` *and* `"method"`),
  answered on the same stream. Note that the server numbers its requests from `0` in a sequence
  of its own, so the ids collide with the client's; the adapter tells the two apart by whether a
  message carries `method` alongside `id`, never by the id itself.
- `thread/tokenUsage/updated` carries `modelContextWindow` (258400 for `gpt-5.6-sol`), which is
  what the context chip needs.
- A `fileChange` item carries its diff twice: on `item/started` (before approval) and again on
  `item/completed`, so the pane can show the edit before the user decides.

## What did not work, or is not what the task assumed

1. **`model/list` does not carry a context window.** Its `Model` objects have `id`, `model`,
   `displayName`, `description`, `supportedReasoningEfforts`, `serviceTiers`, `isDefault`,
   `hidden` — and no token count. `context_pct` is therefore computed from
   `thread/tokenUsage/updated`'s `modelContextWindow`, which is more accurate anyway because it
   follows a model switch. `model/list` is still used, but only by `set_model()` to turn a
   display name into the id codex wants (`"GPT-5.6-Terra"` → `"gpt-5.6-terra"`).
2. **There is no "thread settings update" request.** `thread/settings/updated` exists as a
   *notification*; `ClientRequest.json` has no matching method. The model is switched the way the
   schema documents instead: `turn/start`'s `model` field, whose description is "Override the
   model for this turn **and subsequent turns**". So `set_model()` resolves the name, stores it,
   and the next `send()` carries it.
3. **A `fileChange`'s `diff` is not always a unified diff.** For `kind: {"type":"add"}` it is the
   file's whole new content (`"ok\n"`), not a patch. The adapter puts the `---`/`+++`/`@@`
   headers back for add and delete, and passes an update's body through (prefixing the file
   headers when codex sent only hunks). This is the one place the adapter writes text codex did
   not send.
4. **A turn has more than one agent message.** `phase` is `commentary` or `final_answer` (and may
   be absent — the schema says to treat that as unknown). Every message is streamed as `delta`
   events, but `TurnResult.text` is the `final_answer` messages only, falling back to
   phase-less ones and then to all of them.
5. **`item/permissions/requestApproval` cannot cleanly be refused.** Its response schema requires
   a `GrantedPermissionProfile`; there is no "no" value. The adapter answers a refusal with a
   JSON-RPC error instead, and an allow by echoing the requested profile back with
   `scope: "turn"`. This path is not covered by a recorded fixture.
6. **`codex app-server` logs to stderr and warns at startup.** On this machine every run prints
   `Codex's Linux sandbox uses bubblewrap and needs access to create user namespaces.` — harmless
   under `danger-full-access`, but it is why stderr is kept (last 25 lines) and quoted back when
   the process dies.

## The `--remote` co-attach question

`--listen` does take a websocket address, and the TUI does attach to a running app-server:

```
codex app-server --listen ws://127.0.0.1:8791
  listening on: ws://127.0.0.1:8791
  readyz: http://127.0.0.1:8791/readyz
  note: binds localhost only (use SSH port-forwarding for remote access)

codex --remote ws://127.0.0.1:8791     # in a pty, 100x30
  ╭─ >_ OpenAI Codex (v0.155.1) ─╮   … then the cwd the *server* has, and the server's
  │ model:     loading           │     "Hooks need review" prompt
```

So the transport works and a second client is accepted, at no model cost. Two things are **not**
shown by this and stay open:

- Whether the TUI joins a thread *our* client started (`thread/resume` on a running thread says
  app-server "rejoins that thread", which suggests yes) or only its own.
- The adapter uses `stdio://`, which needs no port and no token. Moving to `ws://` would mean
  picking a port and handling `--remote-auth-token-env`; that is a product decision (it is what
  would let the owner open the Codex TUI onto a pane's live session), not something to build on
  the strength of this probe.

## The fixtures

| file | what it is | lines |
| --- | --- | --- |
| `ok-turn.jsonl` | handshake + one text-only turn | 23 |
| `shell-turn.jsonl` | handshake + a turn that runs `echo` | 45 |
| `interrupt-turn.jsonl` | handshake + a turn interrupted at the first item | 20 |
| `approval-turn.jsonl` | handshake + a `fileChange` approved over the wire | 78 |
| `models.jsonl` | handshake + `model/list` (no model call) | 11 |

Redacted, structure kept: the home directory is `/home/USER`, the recording's working directory
is `/tmp/relay-harness-codex`, `userAgent` loses the terminal and OS build it names,
`installationId` and the remote-control server name become placeholders, and every
`account/rateLimits/updated` payload is replaced with a same-shaped `REDACTED` one (it carried the
plan type and how much of the weekly window was used). Thread, turn and item ids are kept: they
name this recording's session and nothing about the account. Checked with a grep for the owner's
name, `chatgpt.com` and the terminal's name — none appear.
