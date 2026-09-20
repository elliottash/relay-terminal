# Cached-token counts — #GMCF decision 5

> "Measure it. The `usage` event already forwards the provider's whole `usage` object; nobody reads
> `prompt_tokens_details.cached_tokens` … Show it in the Activity pane's turn digest and in
> `session_info`, and the cache becomes a number rather than a belief."
> — `../prompt-distillation/PROPOSAL.md` § 4.2 item 5, § 6 item 5

Decision 4 (the fixed assembly order) is only checkable per provider if the provider's own
cached-prefix count is on screen. It now is.

## What changed

`provider.cache_counts()` normalises every shape a preset can report into one pair on the `usage`
event — `cached_tokens` and, where a provider counts writes separately, `cache_write_tokens`:

| shape | preset(s) |
|---|---|
| `usage.prompt_tokens_details.cached_tokens` | `openai`, `openrouter`, `kimi`/`kimi-code`, `glm`/`glm-coding`, `gemini`, `relay-free`, `local:*` |
| `usage.prompt_cache_hit_tokens` | DeepSeek (reachable through `openrouter` and as a custom provider) |
| `usage.cache_read_input_tokens` / `cache_creation_input_tokens` | Anthropic's own names; `guest:claude` |
| `usage.cached_input_tokens` / `cache_write_input_tokens` | `guest:codex` |
| `timings.cache_n`, beside `usage` in the same object | llama.cpp builds older than the one that added `prompt_tokens_details` |

A key the provider did not report is **absent**, never 0 — the same rule `cost` already followed.
A reported miss *is* 0 and is kept, because "the cache was there and missed" is worth seeing.

`sessions.add_usage` / `load_usage` sum and reload the pair, so `session_info`, the session file and
the per-turn record of protocol 30.5 carry it with no further plumbing.

## What it says per provider

Only the Local tier is verified from this repository; everything else is what the code reads and what
the providers' own documentation claims, as PROPOSAL.md § 4.3 lists it.

| preset | reports a cached count? | evidence |
|---|---|---|
| `local:*` (llama.cpp) | **yes**, both `prompt_tokens_details.cached_tokens` and `timings.cache_n`, and they agree | verified live here, b10706 |
| `guest:claude` | **yes**, `cache_read_input_tokens` + `cache_creation_input_tokens` | recorded fixtures (`tests/fixtures/guest_harness_claude/*.jsonl`) |
| `guest:codex` | **yes**, `cachedInputTokens` + `cacheWriteInputTokens` | recorded fixtures (`tests/fixtures/guest_harness_codex/*.jsonl`) |
| `openai`, `kimi`, `kimi-code`, `glm`, `glm-coding`, `gemini` | documented to, in the OpenAI shape; **unknown** — this repository holds no recorded sample | public docs only |
| `openrouter`, `relay-free` | whatever the upstream route reports, passed through; **unknown per route** | public docs only |
| `anthropic` (the OpenAI-compatible layer), `minimax` | **unknown**; the compatibility layer is documented as not caching at all | public docs only |

Where it is unknown, the pane will now say so by drawing no cache figure at all — which is the point:
the answer arrives the first time the owner runs a turn on that preset.

## Live proof, Local tier (`local:bonsai`, llama.cpp b10706 on 127.0.0.1:8080)

Two turns through the real `ChatProvider` streaming path, the same 726-token prefix, `max_tokens`
256. The script is `live.py` beside this file.

    $ RELAY_KEYRING=off PYTHONPATH=backend python3 live.py
    turn 1: prompt_tokens=726 cached_tokens=0   (raw prompt_tokens_details={'cached_tokens': 0})
    turn 2: prompt_tokens=726 cached_tokens=709 (raw prompt_tokens_details={'cached_tokens': 709})
    session totals: {"prompt_tokens": 1452, "completion_tokens": 61, "total_tokens": 1513,
                     "requests": 2, "cached_tokens": 709}

Before this change the same two turns reported nothing: the `usage` event carried the provider's
object untouched and no surface read `prompt_tokens_details`, so a 97 %-cached request and a cold one
looked identical everywhere in the app.

## On screen

`activity-cached-tokens.png` — the Activity pane, one line per provider call under the turn's rule:
the cold turn, the cached one, an Anthropic-shaped report that counts its writes too, and a provider
that says nothing about caching and therefore claims nothing.

`info-cached-tokens.png` — the ⓘ pane's Tokens row: `120.0k in (96.0k cached) · 8000 out · 128.0k
total · 34 requests`, in the same compact form the row already used.

Both were rendered offscreen by `shot.cpp`; build and run it exactly as
`../../2026-09-20-info-activity-ask-rows/README.md` says.

## Tests

- `tests/test_provider.py::CacheCountsTests` — one per shape, on the recorded samples.
- `tests/test_provider.py::UsageEventCacheTests` — the counts reach the `usage` event from the
  streaming path and the whole-body path, and `timings` never overrides `usage`.
- `tests/test_session_threads.py::UsageTests::test_cached_tokens_are_summed_only_once_a_provider_reports_them`
- `tests/test_guest_harness_provider.py::…::test_both_guests_report_their_prefix_cache_in_the_same_two_fields`
- `tests/test_guest_harness_codex.py::…::test_usage_carries_what_the_prefix_cache_saved`
- `tests/agentinternals_test.cpp` — `aProviderCallSaysWhatItSpentAndWhatWasCached`,
  `aProviderThatSaysNothingAboutCachingClaimsNothing`
- `tests/conversations_test.cpp` — the ⓘ pane's Tokens row, and that a quiet provider draws no
  "(0 cached)".

## One thing that was wrong and is now right

Anthropic counts its cache reads **outside** `input_tokens`, where OpenAI (and codex) count theirs
inside `prompt_tokens`. The claude guest harness's `prompt_tokens` was therefore the *uncached* part
alone: a turn that read 38,991 tokens from the cache and wrote 4,576 was recorded as an 18-token
prompt. `relay_usage` now completes it, so one number means one thing across every preset — and
"in 18 (38,991 cached)" does not appear on screen.
