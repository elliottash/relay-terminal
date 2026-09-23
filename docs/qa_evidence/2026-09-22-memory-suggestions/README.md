# #MEMS part D: memory suggestions in the transcript and in Globals › Suggestions

2026-09-22. Owner decision: "new memories are suggestions that the user confirms. and if the user
rejects, rejections are remembered".

## Live drive (01–05)

The `relay` binary that `scripts/land.py`'s build gate built from the exact tree of `407fc202`, run
on a clean `git archive` of that tree under Xvfb. HOME, XDG_RUNTIME_DIR and TMPDIR were isolated
under `/tmp/md`, with `RELAY_KEYRING=off`, D-Bus disabled and a temporary
`RELAY_GLOBAL_SWITCHBOARD`. Seeds:

- `~/.claude/CLAUDE.md` in the isolated HOME, with two bullets about the user.
- One agent suggestion ("Prefers terse answers with the decision first").
- One agent suggestion already rejected ("Likes tabs over spaces").

| File | What it shows |
|---|---|
| `01-bell-import-notice.png` | Globals was opened through the drive socket (`action globals.open`), which started the tab's worker. Its `configure` now says `memory_import: true`, so the startup import offered the two CLAUDE.md facts and the bell says "2 memories from Claude Code to review · Review". User memory shows "Review 3 suggestions". |
| `02-suggestions-review-list.png` | The bell's Review button opens Globals › Suggestions (3), the list first, with the source of each row: "suggested by an agent" and "imported from Claude Code". The first row is selected, with its origin in the detail area. |
| `03-edit-before-keep.png` | The fact reworded in the editor before Keep. |
| `04-rejected-list.png` | After Keep, edited Keep and No: no suggestions left, and the folded "Rejected (2)" list opened, read-only, with dates and sources. |
| `05-user-memory-after-keep.png` | User memory holds both kept facts as active memories, the edited one in its new wording. |

On disk after the drive: `memory/6QWT.md` and `memory/CD35.md` are active (CD35's body is the
edited sentence), `memory/rejected/` holds both rejections, and `memory/suggestions/` is empty.
`memory_suggestions.suggest("I prefer British spelling in prose.")` afterwards returned
`declined`, matching rejection `175N`.

## Transcript line (06–07)

`relay-consolemode-tests --memory-only` builds a real `Pane` (a console) and feeds it
`tool_result` events for `app_user_memory` shaped as `backend/relay_core/app_tools.py` returns
them. `RELAY_MEMORY_LINE_CAPTURE` and `RELAY_MEMORY_OUTCOME_CAPTURE` save the two images.

- `06-transcript-remember-line.png` and `07-transcript-outcomes.png`: under each call row sits
  "✦ Remember: <fact>   Keep · Edit · No  (Ctrl+click)". Keep, Edit and No are OSC 8 links to
  `relay://memory/<pane>/<word>/<id>`. A declined repeat is the quiet note "Not suggested: Likes
  tabs · you rejected it on 2026-09-21". After Keep and No the transcript adds "Kept: … · Globals
  › User memory" and "Rejected — won't be suggested again: …". Clicking the old links again sends
  nothing, which the test asserts.
