---
id: MVGR
type: work
status: needs-verification
assignee: agent
implemented_by: glm/glm-5.3
priority: 1
rank: zzzzzzzzzzzzzy
created: '2026-09-19'
links: {plans: [], commits: [192bf4c0], evidence: [docs/qa_evidence/2026-09-20-recap-finished-at/], related: [], github: null}
---
# when a recap shows up after an agent finishes, include at the beginning, finishe…

## Issue
when a recap shows up after an agent finishes, include at the beginning, 

[end of meassage]

finished at [time]

[recap]

## Plan
## Goal

When a recap is printed after the agent has finished, the recap block starts with `[end of message]` and `finished at HH:MM` lines ahead of the `Recap · …` header, so a returning reader can see where the agent's last message ended and when. The times come from the recorded turn stamps, never the model (the existing recap rule, 2026-09-17).

## Findings

- The recap that "shows up after an agent finishes" is the **away** recap: `Pane::noteWindowActivation(bool)` (`src/Pane.h:7829`) starts `m_awaySince` when the window deactivates, a turn ending while inactive sets `m_finishedWhileAway` (`src/Pane.h:9249`), and refocus after a threshold sends `recap_request {reason: "away"}` (`src/Pane.h:7841-7843`, gated by Options › General `recap/away`). Resume (`_resume`) and manual (`/recap`, `requestRecap`) recaps print through the same handler.
- Worker: `SessionCommands._start_recap` (`backend/relay_core/session_protocol.py:453`) snapshots messages plus checkpoint stamps and calls `suggestions.recap()` (`backend/relay_core/suggestions.py:91`) on a background thread. The event carries `text`, `next_action`, `turns_covered`, `reason`, `open_items`, and `span_fields()` output (`suggestions.py:82`): `span_start`, `span_end` (epoch), `span_seconds`, `span_text`. All clock formatting lives in Python (`format_span`, `_clock`, `_dated`) off the turn stamps; the model is told never to state times.
- GUI: the `recap` handler (`src/Pane.h:6583-6613`) prints `Recap · <span_text>`, the summary, `Next · …`, `Open · …`, each `printInline(..., Ink::Recap)`; `Ink::Recap` opens one `relay::gaps::Block::Recap` (`src/Pane.h:11513`), so the block already gets its blank line from the agent prose above (`tests/transcriptgaps_test.cpp`, `theRecapIsItsOwnBlock`).
- `span_end` is exactly "when the agent finished" (the last turn's `ended` stamp, `checkpoints.end_turn`), but it is an epoch number; the GUI has no local-clock formatter, and the house pattern is that Python formats recap times.
- Side fault noticed (not this card): the phone's recap hook (`src/Pane.h:5704`) sends `reason: "remote"`, which `_recap_request` rejects (`RECAP_REASONS` is away/resume/manual) — that button errors. File a separate bug card.

## Steps

1. `backend/relay_core/suggestions.py` — in `span_fields()`, add `finished_text`: the local-clock text of the span's end (`_clock(end)` when the end is today, `_dated(end)` otherwise, same `now` parameter as `format_span`). Include it only when the last turn is actually finished — the last item carrying a `time` stamp also carries `ended` — so a manual recap mid-run (a running turn contributes only its start to `span`) prints no finish time it cannot honestly state. Comment it as the owner request of 2026-09-19 (this card). `recap()` spreads `**span_fields(...)`, so the field rides every recap event with a span; the `_start_recap` failure path stays without it.
2. `src/Pane.h`, `recap` handler (~6596-6606) — before the `Recap · ` header lines, `printInline(QStringLiteral("[end of message]\n"), Ink::Recap)` and, when `finished_text` is non-empty, `printInline(QStringLiteral("finished at %1\n").arg(finished), Ink::Recap)`. Same `Ink::Recap`, so the preamble joins the one `Block::Recap` and the gaps rules keep the single blank line above the block. Update the comment above the header code to say what the block now opens with.
3. `docs/AGENT-SESSIONS-PROTOCOL.md` §5 — add a bullet after "Recap span" in the same style: the `recap` event also carries `finished_text` (local-clock text of `span_end`, present only when the last turn ended), and the GUI prints `[end of message]` / `finished at …` ahead of the `Recap ·` header (owner, 2026-09-19).
4. Tests:
   - `tests/test_sessions.py::RecapSpanTests::test_span_fields` — expect `finished_text: '11:47'`; add a not-today case (dated form) and a last-turn-running case (no `finished_text`).
   - `RecapSpanAgentTests::test_turns_are_stamped_and_the_recap_carries_the_span` — assert `finished_text` is present with stamps and absent in the no-stamps recap.
   - `tests/test_session_protocol.py` (`test_compact_resume_recap_and_plan_execute`, ~line 240) — assert the resume recap carries `finished_text`.
5. Land: `scripts/relay-build`, the targeted tests below, `python3 scripts/land.py begin` / `commit`, card to needs-verification with QA evidence per `issues/README.md`.

## Risks

- **Layout** (owner question): the sketch's blank lines are read as paragraphing. Planned: two adjacent lines, no blank lines between them and the header — blank lines between *blocks* are TranscriptGaps' business, and `Recap ·`/`Next ·`/`Open ·` are already adjacent. If you want the three parts spaced as separate paragraphs, that needs new `Block` kinds in `relay::gaps` — say so and step 2 grows.
- **Scope** (owner question): the preamble prints for every recap reason (away, resume, manual), one code path. If you want it only on the away recap, it is one `reason` check in step 2.
- A recap with no stamps (old session files) prints `[end of message]` alone — the marker states a fact about the transcript; the time is simply unknown.
- The phone fallback transcript (`app/app.js`, `case 'recap'`) prints only `event.text` and is left unchanged: a phone normally reads the pane's terminal stream, where the preamble already shows. Parity there is a two-line follow-up if wanted.

## Verify

- `pytest tests/test_sessions.py -k RecapSpan` and `pytest tests/test_session_protocol.py -k recap`.
- `ctest --test-dir build -R TranscriptGaps` — block separation unchanged.
- Live under Xvfb with an isolated `XDG_CONFIG_HOME`: run 3+ turns, unfocus the window while the last one runs, refocus — the recap block reads `[end of message]` / `finished at HH:MM` / `Recap · …` under a blank line; check `/recap` and resume-a-session paths too, and that a mid-run `/recap` omits the `finished at` line.

## QA checklist

- [ ] Away recap, live: 3+ turns, the last finishing while the window is away — refocus prints
      `[end of message]` / `finished at HH:MM` / `Recap · …` as one block under a single blank
      line (implementer run and reproducer: `docs/qa_evidence/2026-09-20-recap-finished-at/`,
      `drive.sh <build-dir>`).
- [ ] `/recap` by hand and a resumed session's recap print the same preamble (one code path).
- [ ] A recap asked while a turn still runs prints `[end of message]` with **no** `finished at`
      line (slash-list Enter reaches it mid-run; Ctrl+Return would steer instead).
- [ ] A session whose turns carry no stamps (an old session file) prints `[end of message]` alone.
- [ ] `tests/test_sessions.py` RecapSpan cases, the `test_session_protocol.py` recap case, and
      `ctest -R transcriptgaps` green.

