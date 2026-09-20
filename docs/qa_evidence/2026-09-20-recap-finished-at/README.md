# #MVGR — recap opens with `[end of message]` / `finished at HH:MM`

Implementer evidence for `issues/features/2026-09-19-when-a-recap-shows-up-after-an-agent-finishes-in.md`
(status → needs-verification). Implementer: `glm/glm-5.3` (Relay pane agent, preset `glm-coding`).

## The change

- `backend/relay_core/suggestions.py` — `span_fields()` adds `finished_text`: the span's end in
  the worker's local clock (`_clock` today, `_dated` otherwise), present only when the last
  stamped turn also carries an `ended` stamp, so a recap asked for mid-run states no finish time.
- `src/Pane.h` (recap handler) — prints `[end of message]` and `finished at …` ahead of the
  `Recap ·` header, same `Ink::Recap`, one `Block::Recap`.
- `docs/AGENT-SESSIONS-PROTOCOL.md` §5 — new "Recap finish" bullet.
- Tests: `tests/test_sessions.py` (`test_span_fields` gains the today/dated/running cases;
  `RecapSpanAgentTests` asserts the field present with stamps, absent without),
  `tests/test_session_protocol.py` (the resume recap carries it).

## Machine evidence

- `scripts/relay-build` — 45 s, green (`2026-09-20.01H.07`).
- `python3 -m unittest tests.test_sessions.RecapSpanTests tests.test_sessions.RecapSpanAgentTests`
  — 6 tests OK (pytest is not installed on this machine; the house runner is `scripts/test.sh`'s
  unittest discovery).
- `python3 -m unittest tests.test_session_protocol.ProtocolHandlerTests.test_compact_resume_recap_and_plan_execute`
  — OK.
- `ctest --test-dir build -R '^transcriptgaps$'` — Passed (the recap block keeps its single
  blank line; no new `Block` kinds).

## Live under Xvfb (`drive.sh <build-dir>`, OCR-checked)

Loopback stub provider (`recap-stub-provider.py`), isolated `HOME`/`XDG_*`, no network, no key,
`RELAY_RECAP_AWAY_SECONDS=2`. `run-run2.log` holds the full OCR; `run-first-attempt.log` the
first attempt (kept deliberately — see Findings). All checks PASS:

- **Away recap (the card's scenario):** three fast turns, a fourth held 9 s by the stub so it is
  still running when focus leaves, ending while the window is away; refocus prints
  (`implementer-03-away-recap.png`):

  ```
  [end of message]
  finished at 01:38
  Recap · 01:38 → 01:38 · <1m
  Four stub turns ran; the last finished while the window was away. Nothing is unverified
  ```

  (OCR renders `·` as `+`/`-`; the screenshot shows the real glyphs.)
- **Manual `/recap`:** the same preamble (`implementer-04-manual-recap.png`).
- **Mid-run `/recap`:** a slow turn submitted, `/recap` + Enter (the slash list's Enter) while it
  runs — the block prints `[end of message]` with **no** `finished at` line
  (`implementer-05-midrun-recap.png`, bottom crop).

## Findings while driving it

- The first attempt raced: an instant stub turn finished before the focus change landed, so no
  away recap fired. The drive now uses a slow turn — the app behaved correctly all along.
- While the agent is busy the prompt box is a queue: **Ctrl+Return escalates a steer** (it
  interrupted the running turn and `/recap` never ran as a command). Plain Enter — the slash
  list's own key — runs `/recap` mid-run fine. Worth knowing, not a fault of this change.
