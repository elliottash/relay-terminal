# #T71W backend: the signature, the ranking and the `qa` block — implementer evidence

Date: 2026-09-19. Implemented by `anthropic/claude-opus-5` (subagent of the Claude Code session
that owns the card). Tasks `t:a1`–`t:a4`. The GUI half (`t:b2`) and the briefs (`t:b1`) are
another session's.

## What landed

| File | What |
|---|---|
| `backend/relay_core/qa_verifiers.py` | new. `signature`, `family`, `lineage`, `VERIFIER_RANK`, `recommend`, `summary_line`, `availability`, the commit-trailer readers |
| `backend/relay_core/board_tools.py` | `model_family` delegates; `ToolContext.preset` and `.signature()`; `_move` stamps `implemented_by` and `verified_by`; `board_read` gains `qa`; the row gains `verified_by` |
| `backend/relay_core/board.py` | `verified_by` in `WORK_FIELDS` and `FIELD_ORDER`, so `check` accepts it and a rewrite keeps it in place |
| `backend/relay_core/board_protocol.py` | `verified_by` on every row; `board_card_get` carries the `qa` block because it *is* `board_read` |
| `backend/relay_core/agent.py` | `Agent.sign_board()`: the board context learns the pane's preset as well as its model, every turn |
| `scripts/relay-board.py` | `verifier <ID> [--json]` |
| docs | protocol §19.15, `SWITCHBOARD-FORMAT.md` §2.2, `SWITCHBOARD-DESIGN.md` §6.5, `board_policy.md` rule 5 |

## The decisions inside it, and why

- **The worker's stamp beats the agent's argument.** `implemented_by` used to be whatever the model
  typed. Now `board_move_card` writes `provider/model` from `ToolContext.preset` + `.model` on entry
  to `in-progress` and to a QA lane, and `verified_by` on leaving a QA lane to `done`. The agent's
  own `implemented_by` argument is honoured only when the worker has no signature at all — a guest
  CLI writing through the bridge. The refusal that asks for `implemented_by` therefore only fires in
  that case now, which is what `test_a_qa_lane_needs_evidence_and_an_implementer` was changed to say.
- **One family table, so the two spellings of a model agree.** `model_family("Claude Opus 5 (pane 2)")`
  returned `claude` while `model_family("anthropic/claude-opus-5")` returned `anthropic`: two names
  for one lab, and the independence rule let each close what the other wrote. Both are `anthropic`
  now, and the old assertion was replaced by an equality between the two forms — that equality is
  the property that matters.
- **The vendor is the model's, not the route's.** `openrouter` + `deepseek/deepseek-v4.1-flash`
  signs `deepseek/deepseek-v4.1-flash`; `google/gemini-3.8-flash` through OpenRouter signs
  `gemini/…`. An unknown model still falls back to its provider segment, so a provider the table has
  never heard of still tells itself apart from another one.
- **Relay Free is a route, not a lab.** Its family and lineage resolve through `RELAY_FREE_UPSTREAMS`
  (read off `gateway/gateway.example.json`, 2026-09-19): `relay-main` → GLM-5.3 Flash (cn-open),
  `relay-flash` → DeepSeek V4.1 Flash (cn-open), `relay-lite` → Gemini 3.5 Flash Lite (google). So a
  card written on Relay Free cannot be closed by GLM, a GLM card is not offered Relay Free, and the
  row's label says which upstream it is. The signature itself stays `relay-free/relay-main`: it
  records the route the owner chose, and the upstream is resolved at the moment a verifier is picked,
  which is the moment the answer has to be current. `gateway/proxy.py` does pass the upstream's own
  `model` string through to the client, but nothing in the worker records it today; when something
  does, prefer it over the table (noted in the module).
- **Lineage before capability.** The implementer's family is skipped outright; every *other* lineage
  comes first in the owner's order; the implementer's own lineage follows, still offered, saying so;
  a local endpoint is last whatever its lineage. Sources per row in `LINEAGE` and `VERIFIER_RANK`,
  from `research-cross-model-qa.md` in this folder.
- **Availability is probed once a minute, not per read.** `keystore.available()` shells out once per
  preset; a card detail must not pay that every time it is opened. Guests are found with
  `shutil.which` over `relay_core.guest.GUESTS[*].binaries` — the registry, never a hard-coded list,
  and never a `--version` subprocess, which a hung CLI would stall a board read on.
- **The rows stay light.** `verified_by` is on every row; the `qa` block is not — it is per card, in
  `board_read`/`board_card_get` only.

## The `qa` block this emits

`implementer-backend-sample-qa.json` in this folder is a real `board_read` answer for a card
implemented by `anthropic/claude-opus-5`, on a machine with Codex on PATH, GLM Coding Plan and
OpenRouter keys stored, Relay Free on and one local endpoint. Codex is recommended; GLM, DeepSeek,
Relay Free (GLM-5.3 Flash) and the local model follow in that order; Claude is skipped because it
implemented the card; Kimi, Gemini and MiniMax are unavailable for want of a key.

## Tests

- `tests/test_qa_verifiers.py` (new, 35 tests): signature for every shipped preset and tier, the
  family equivalences, the lineage table, the ranking under eight availability shapes, the commit
  trailers against a real temporary git repository, and the shape of the availability probe. Every
  ranking test passes availability in, so none of them reads this machine.
- `tests/test_board_tools.py`: the `SignatureTests` class (8 tests) — stamping on `in-progress`,
  the stamp beating a typed value, a guest still naming itself, `verified_by` on close plus a clean
  `board.check()`, the Relay Free refusal, the `qa` block on `board_read`, no block without an
  implementer, and the row carrying the signatures but not the block. Two existing tests updated as
  described above.
- `tests/test_board_protocol.py`: `board_card_get` carries the block and the rows do not; the board
  context learns the pane's preset.
- Full `./scripts/test.sh` and `PYTHONPATH=backend python3 scripts/relay-board.py check` below.
