# Card #Z79Y — the output token limit is per model

Implementer evidence, 2026-09-18. `drive.py` resolves a real `ProviderConfig` for every preset and
then sends one real request per preset through the real `ChatProvider` to a loopback mock, printing
the `max_tokens` that arrived in the body. No key and no network. `per-model-limits.log` is its
output.

```
XDG_DATA_HOME=$(mktemp -d) RELAY_KEYRING=off PYTHONPATH=backend \
  python3 docs/qa_evidence/2026-09-18-output-token-limit-is-per-model/drive.py
```

What the log shows:

- **Automatic gives each model its own documented number.** GLM-5.3, Kimi K3, Claude Opus 5 and
  MiniMax M3 get 131,072; GPT-6 Astra 128,000; Gemini 3.1 Pro 65,536; OpenRouter and any endpoint
  Relay cannot name 32,768; a local server a quarter of its served window.
- **Pinning Relay's ceiling never sends a model more than it takes.** The `pinned 131072` column is
  identical to `automatic` for every preset: a request above the cap is refused outright, so the
  clamp is what keeps a pinned setting working after a provider switch.
- **A smaller number is the user's own choice and is kept** (`pinned 8192`), and on an endpoint Relay
  cannot name even a large one is sent as typed — that base URL is the user's own.
- **Why not 10% of the window.** The last line of part 1 prints it: ~100K for every built-in preset,
  because they all have ~1M windows, so it is indistinguishable from a flat 128K where the flat rule
  works — and still wrong for Gemini, which documents 65,536 on a 1,048,576-token window. On the
  128,000 fallback window a custom endpoint would get 12,800, and a local 32K server 3,276.

Gemini is the case that decides the design: a **larger** context window than GLM-5.3 and **half** the
output cap. Any rule that derives output from the window gets it wrong.
