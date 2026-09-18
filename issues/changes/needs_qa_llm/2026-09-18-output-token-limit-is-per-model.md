---
id: Z79Y
type: work
status: needs-qa-llm
component: [gui, worker, providers]
milestone: desktop-alpha
workstream: providers
assignee: agent
implemented_by: Claude Opus 5 (1M context), Claude Code session, 2026-09-18
rank: zzz
created: '2026-09-18'
labels: [change]
acceptance: 'with no saved `provider/max_tokens`, a GLM-5.3 pane requests `max_tokens: 131072`, a Gemini pane 65536, an OpenRouter or custom endpoint 32768, and a local server a quarter of its window; a pinned number is kept but never sent above the model cap; a saved 32768 migrates to automatic once; `ctest` and `./scripts/test.sh` pass'
source: 'owner, 2026-09-18: "boost default output to 128K for all models -- or should we say 10% of context length?"'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-output-token-limit-is-per-model/'], related: [G5MK, 27YQ], github: null}
---
# The output token limit is 128K where 128K is allowed, and each model's own number everywhere else

## Issue

boost default output to 128K for all models -- or should we say 10% of context length?

The limit had been 32768 for every provider, which is a quarter of what GLM-5.3 allows and was also
the top of the range, so a turn that ran out of output had nowhere to go (#G5MK).

## What the numbers say

Neither rule survives contact with the preset table. Output caps are **published per model** and
have no fixed relationship to the context window:

| Preset | Model | Context window | Documented output | Source |
|---|---|---|---|---|
| `glm`, `glm-coding` | GLM-5.3 | 1,000,000 | 131,072 | https://docs.z.ai/guides/llm/glm-5.3 |
| `kimi`, `kimi-code` | Kimi K3 | 1,048,576 | 131,072 | https://platform.kimi.ai/docs/guide/kimi-k3-quickstart |
| `anthropic` | Claude Opus 5 | 1,000,000 | 131,072 | https://platform.claude.com/docs/en/models/opus-5/overview |
| `minimax` | MiniMax M3 | 1,000,000 | 131,072 recommended | https://platform.minimax.io/docs/guides/text-generation |
| `openai` | GPT-6 Astra | 1,050,000 | 128,000 | https://developers.openai.com/api/docs/models/gpt-6-astra |
| `gemini` | Gemini 3.1 Pro | 1,048,576 | **65,536** | https://ai.google.dev/gemini-api/docs/gemini-3 |
| `openrouter` | DeepSeek V4.1 Flash | 1,048,576 | route-dependent | endpoint caps differ; OpenRouter's DeepInfra route for Kimi K3 allows 16,384 |

**Gemini decides it.** It has a *larger* window than GLM-5.3 and *half* the output cap. A flat 128K
default asks it for twice what it takes; "10% of the context window" asks it for 104,857, which is
also over. A request above the cap is refused outright, not trimmed, so both rules would turn every
Gemini pane into an error. The percentage rule fails at the other end too: every built-in preset has
a ~1M window, so 10% is ~100K for all of them — indistinguishable from the flat rule where it works
— while a custom endpoint on the 128,000 fallback window would get 12,800 and a local 32K server
3,276, both far too small for one coding step.

So the answer is the owner's first instinct with one correction: **128K wherever 128K is documented,
and the model's own number everywhere else.** The word "all" is the only part that had to go.

## Change

`max_tokens` is now **0, or 256–131072**, and **0 is the default and means automatic**: ask this
model for what its own documentation allows.

- **`presets.py`**: `Preset.max_output`, filled in per entry with the doc URL beside it, exactly as
  `context_window` already was; `DEFAULT_MAX_OUTPUT` (32,768) for anything Relay cannot name;
  `max_output_for()` and `resolve_max_tokens()`. `to_dict()` carries it, so the GUI can show it.
- **`provider.py`**: `ProviderConfig.__post_init__` settles the number once, at construction, so the
  request, the compaction reserve and the model-switch ceiling all read the same value. A pinned
  number is kept but never sent above the model's cap. An endpoint Relay cannot name keeps whatever
  it was given: the user typed that base URL and knows what it takes. A local server still gets a
  quarter of its served window. `MAX_OUTPUT_TOKENS` is 131,072.
- **`localmodels.clamp_max_tokens`** passes 0 through, so automatic reaches the resolver.
- **The truncation message** (#G5MK) no longer says "already at Relay's maximum": a Gemini pane at
  65,536 is told that is as much as the model gives, because raising Relay's limit would change
  nothing for it.
- **GUI**: the Options row and the provider dialog's spin box go to 0–131072, and the spin box shows
  `Automatic (the model's own limit)` instead of a zero. `migrateOutputTokenCeiling()` turns a saved
  **32768** — the old default, which was also the old top of the range, so anyone holding it wanted
  all there was — into 0, once, at startup. Any other saved number was chosen on purpose and stands.

Effect at the default: GLM-5.3 and Kimi K3 **131,072**, Claude Opus 5 131,072, MiniMax M3 131,072,
GPT-6 Astra 128,000, Gemini 3.1 Pro 65,536, OpenRouter and custom endpoints 32,768 as before, a
local server a quarter of its window as before. Auto-compaction is unmoved: at a 1M window the
reserve formula (`window − max_tokens − 24K` = 844,928) still loses to the 80% soft limit (800,000).

Files: `backend/relay_core/presets.py`, `provider.py`, `localmodels.py`, `session_protocol.py`,
`src/main.cpp`, `src/Pane.h`, `src/RelayWindow.h`, `tests/test_presets.py`, `tests/test_provider.py`,
`docs/AGENT-SESSIONS-PROTOCOL.md` (new 13.10), `docs/ARCHITECTURE.md`.

## Deliberately left

- **`relay-free` gets no entry of its own.** The preset is another session's uncommitted work; the
  conservative fallback is the right answer for it anyway, since the gateway rebuilds the request and
  owns its own per-role output cap (`docs/RELAY-FREE.md`). Nothing needs doing when that lands.
- **The protocol doc numbers this section 13.10** although 13.9 is not committed yet, so the two
  sessions cannot collide on a heading number.

## QA checklist

1. Fresh `XDG_CONFIG_HOME`: Options › Models shows "Output token limit" 0 with the automatic wording;
   Advanced provider settings shows `Automatic (the model's own limit)`.
2. Point a pane at a mock endpoint and send a prompt on each preset in turn: the request body carries
   131072 for GLM-5.3, 65536 for Gemini, 32768 for OpenRouter. Same after switching with the chip.
3. Set the limit to 8192, restart: it stays 8192 and is sent on every preset.
4. Set the limit to 131072 on a Gemini pane: the request still carries 65536.
5. Put `provider/max_tokens=32768` in `relay.conf` by hand, start Relay: it becomes 0 and the GLM
   pane requests 131072. Put 8192 there instead: it stays 8192.
6. `./scripts/test.sh` and `ctest --test-dir build` pass.
