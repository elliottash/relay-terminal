# Staging notes — #RKP3

The staged Relay runs with a throwaway profile under `/tmp/claude-1000/tryit/rkp3`, so your real
providers, keys and priority lists are untouched. The seeded models (`claude-h1`, `claude-m1`, …)
are invented ids with no provider behind them, so every row is greyed "unavailable" — that is the
one visible difference from real use, and it changes nothing about what is being tried: the ▲▼
buttons, dragging between sections and the footer line all work the same on greyed rows (verified
in `docs/qa_evidence/2026-09-22-rank-reorder-repro/`). The binary is `build/relay` of this
checkout at 02c32b78. Deleting `/tmp/claude-1000/tryit` removes the fixture completely.
