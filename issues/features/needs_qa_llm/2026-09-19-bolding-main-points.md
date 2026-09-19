---
id: CVHT
type: work
status: needs-qa-llm
assignee: agent
implemented_by: claude
rank: zzzzzt
created: '2026-09-19'
links: {commits: [168f6ed], evidence: [docs/qa_evidence/2026-09-19-bolding-main-points/], github: null, plans: [], related: [4E13]}
---
# bolding main points

## Issue
encourage agents to put the main point in bold, especially saying when they are done, when they find a problem and when saying what they need from the user. 

is it possible to add colored bolds for that? eg purple for done, blue what they need form the user, and red for problems.

## Plan
## Goal

Two halves of one habit: agents put the reply's main point in bold — above all when they say they are **done**, report a **problem**, or state what they **need** from the user — and the terminal renders those three bolds in colour (purple done, blue need, red problem), so the main point is findable at a glance in the scrollback.

## Findings

- Every pane agent's system prompt is `SYSTEM` in `backend/relay_core/agent.py` (assembled by `Agent.system_prompt()`); subagents share it (their preamble in `backend/relay_core/subagents.py` is added on top). The Markdown advice is one line: "Format replies as Markdown; …". The comment above `SYSTEM` says one sentence per line, deliberately — add rules as whole lines.
- Agent prose is rendered as it streams by `relay::MarkdownAnsi` (`src/MarkdownAnsi.h/.cpp`, used at `src/Pane.h:8405–8415`): Markdown in, ANSI out. Bold is just `;1` appended in `style()`; there is no coloured-bold syntax in Markdown.
- The `Palette` defaults name the terminal's own colours — ANSI indices (35 magenta, 33, 36, 34), not RGB — because an absolute colour is burnt into the scrollback and broke on theme switch (owner report 2026-09-18; `tests/theme_test.cpp` asserts AA contrast for ANSI 1–7). New colours must stay indexed; theme override stays possible via `Palette`.
- Existing hooks to copy: the `[link](url)` branch in `inlineStep()` already holds back a bounded prefix of a line until it can classify it, and `finish()`/`holding()` flush anything held.
- Tests: `tests/markdownansi_test.cpp` (rendered ANSI, streamed == whole-text, palette override, no-truecolor default) and `tests/test_agent.py::SystemPromptTests` (asserts the Markdown line of `SYSTEM`).

## Steps

1. **Prompt** — `backend/relay_core/agent.py`: add one line to `SYSTEM` right after the "Format replies as Markdown" line, e.g. "Lead with the main point in bold when you finish, hit a problem, or need something from the user — `**Done:**`, `**Problem:**`, `**Need:**` labels — the terminal colours those three." One sentence, one line, per the comment above `SYSTEM`.
2. **Palette** — `src/MarkdownAnsi.h`: add `QString done = "1;35";` (magenta, the agent's violet ≈ the asked-for purple), `QString need = "1;34";` (blue), `QString problem = "1;31";` (red) to `Palette`, with a comment naming card #CVHT and why they are indexed. Add private state for the open bold run: a small held-prefix buffer and a `m_boldRole` (none/done/need/problem).
3. **Classification** — `src/MarkdownAnsi.cpp`: one static keyword table in the anonymous namespace, matched case-insensitively against the first word (colon/punctuation stripped) of a bold run:
   - done: `done finished complete completed ready success passed fixed works working`
   - problem: `problem error failed failure broken blocked warning bug`
   - need: `need needs question waiting ask decision`
   In `inlineStep()`'s `*`/`_` branch: when a bold run opens, hold emission of its first ≤32 characters (enough for two words, the `[link]` hold pattern) or until the run closes; classify, emit the held text in the role's colour, and let `style()` keep that colour for the rest of the run. Unmatched runs render as plain bold, exactly as today. Reset the role when the run closes, at line end (`resetInline()`), and in `finish()`; count a non-empty hold in `holding()`.
4. **Tests** — `tests/markdownansi_test.cpp`: bold `**Done:** …` / `**Need:** …` / `**Problem:** …` produce `1;35` / `1;34` / `1;31`; `**Bold**` stays `;1` with no role SGR; add labelled bold to the `streamingMatchesWholeText` corpus; extend `everyColourComesFromThePalette` with the three new fields; the no-truecolor default assertion must still hold. `tests/test_agent.py::SystemPromptTests`: assert the new bold line.

## Risks

- **Keyword heuristic**: a bold run that merely *starts with* a matching word ("**Error codes** …") gets coloured. Kept small by the short closed lists and the leading-word match only. Alternative — colour only bold runs that open a line or bullet — is stricter but misses mid-sentence main points; recommend the simple version first.
- **Colours** (answered: indexed, and green/amber/red per #4E13): exact purple would need RGB, which is burnt into scrollback and broke theme switching (2026-09-18 report). The plan uses the theme's indexed magenta/blue/red instead — *question for the owner*: indexed colours as above, or literal purple/blue/red RGB regardless of theme?
- The held prefix adds ≤32 chars of streaming latency inside a bold run — imperceptible, and `finish()` already flushes held text.
- Other surfaces (turn/subagent transcripts, exported text) show literal `**` today and are unchanged by this card.

## Verify

- `./scripts/test.sh` and `ctest --test-dir build` (new `markdownansi_test` cases and the `SystemPromptTests` assert).
- Live under Xvfb with an isolated `XDG_CONFIG_HOME`: a reply containing `**Done:**`, `**Problem:**`, `**Need:**` and a plain `**Bold**` shows three colours plus plain bold; switch theme and confirm the scrollback recolours (no burnt-in RGB).

## QA checklist
- [ ] `backend/relay_core/agent.py`: `SYSTEM` carries the new bold line right after the Markdown line, one sentence on one line, and the three labels read `**Done:**`, `**Problem:**`, `**Need:**` (case as written).
- [ ] `src/MarkdownAnsi.h`: `Palette::done/need/problem` default to `1;32` / `1;33` / `1;31` —
      green, amber, red — indexed ANSI, no RGB, no `38;2;`/`38;5;`, and the comment names card #CVHT
      and the reason. **The plan's magenta/blue/red was overtaken while this card was open**: card
      #4E13 (`be81edb`, "Amber means one thing") made amber the one colour for "something is waiting
      on you", so `**Need:**` is amber and `**Done:**` takes the Done glyph's green. That answers the
      plan's open colour question for the labels; the *indexed, not RGB* rule it also fixed stands.
- [ ] `src/MarkdownAnsi.cpp`: the match is on the bold run's *first* word only, case-insensitive, colon and punctuation stripped; the lists are the three the plan fixed.
- [ ] The held prefix is bounded (≤ 32 chars) and flushed at the next inline marker, at a line end, in `finish()` and in `resetInline()`; `holding()` counts it. Feed `"**Done"` and then `finish()`: the text must come out, in the role colour, and nothing may be swallowed or duplicated.
- [ ] Unlabelled bold is untouched: `**Bold**`, `***back***`, `snake_case`, `2 * 3 * 4` all render exactly as before, with no role SGR.
- [ ] `build/relay-markdown-tests` (14 cases) and `tests/test_agent.py` (30 cases) pass; `ctest --test-dir build` shows 53/54, the one failure being `backend-and-bash` on the four cases that fail on `HEAD` already (`test_roles` planning, `test_sessions` compaction, `test_remote_wire` `plan_route`/`question` events) — confirm they are untouched by this card and not newly caused.
- [ ] Live: `docs/qa_evidence/2026-09-19-bolding-main-points/drive.sh <build-dir>` under Xvfb — the reply's three labels come out in three distinct colours, the unlabelled `**Bold**` does not, and with the `qa-bold` theme's own `terminal.palette` the same bytes come out in *that* palette's colours (no burnt-in RGB). Compare against `implementer-notes.txt`.
- [ ] Read the reply as text: the labels are findable at a glance and the markers are gone (`Done:`, not `**Done:**`).
- [ ] Prompt effect: a fresh agent reply that finishes, reports a problem and asks for something uses the three labels (the stub provider proves the renderer, not the model's obedience; note which half the check covers).
- [ ] Decide the open risk: a bold run that merely starts with a listed word (`**Error codes** are…`) is coloured — acceptable, or tighten to line-opening bolds?
