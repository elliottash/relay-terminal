---
id: 5AWD
type: work
status: needs-qa-llm
labels: [feature, terminal]
assignee: agent
implemented_by: glm/glm-5.3-flash
rank: zzzzzzzi
created: '2026-09-19'
links: {commits: [b25fd164, 8147cc55, 3e12fa58], evidence: [docs/qa_evidence/2026-09-19-helpful-line-breaks/README.md], plans: [], related: [], github: null}
---
# helpful line breaks in the terminal

## Issue
add line breaks in the terminal between content types. 

there should be a line break between agent messages and tool calls for example. sequences of tool calls are single-spaced. ditto with user messsages, line breaks between them (could be half spaced if that looks better)

## What "done" means
One blank line between blocks of different kinds in the terminal transcript: the ✦ line the user
typed, the `▸ model` header, the agent's prose, the ▸ tool-call rows, the "✦ N tool calls" link that
sums a run of them up, and a "Recap ·" block. Never between two blocks of the same kind (a run of
tool rows stays single-spaced, a recap's own lines stay together), never right after the header (it
sits on top of what follows it), and a ✦ line is set off from the previous turn even across the
shell prompt between them. Notes, errors, inline diffs and streamed tool output carry no kind and
stay attached to the block above. The "✦ N tool calls" link is a Call block: it is set off from the
prose above it, but it crowns a run of tool rows with no extra gap. "Half spacing" is not available
on a terminal grid, so the gap is a full blank row. Rule: `src/TranscriptGaps.h`; hooks:
`Pane::beginBlock` in `src/Pane.h`.

## QA checklist
- [ ] `ctest --test-dir build -R transcriptgaps` passes (the rule, headless).
- [ ] Run `docs/qa_evidence/2026-09-19-helpful-line-breaks/drive.sh`: in `implementer-01-walk.png`
      there is one blank row between the ✦ line and `▸ stub`, none between `▸ stub` and the first
      prose line, one between prose and the tool rows, none between `▸ read 2 files` and
      `▸ ran ls -la`, one between the rows and the next prose, and one between the closing prose
      and `✦ N tool calls · …`.
- [ ] `implementer-02-second.png`: the second ✦ line has a blank row above it, and the turn's
      `▸ stub` / prose follow with one gap between the ✦ line and the header.
- [ ] `implementer-03-tools.png`: a turn whose first step is a tool call shows `▸ stub` directly
      above the row (no gap), then a gap before the closing prose.
- [ ] `implementer-04-recap.png`: after the turn, `Recap · …` is set off from the `✦ N tool calls`
      link above it by one blank row.
- [ ] No stale `▸ reading …` / `▸ editing …` row is left above a finished row: each running row is
      rewritten in place.
- [ ] With a real provider (any key): a steer typed mid-turn (`✦ …`) gets a blank row above and
      below it; a thinking model shows `▸ model` then `▸ ✦ thinking…` with no gap between.
- [ ] `clear` in the shell, then a prompt: the transcript does not begin with a blank row.

