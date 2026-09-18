# A card's title and its issue text are edited on the card (2026-09-18)

Owner: *"after adding a card, i couldn't edit the title or the task. i'm not sure about 'request'
there, let's call it issue."*

Two things. **Editing never existed in the pane**: the card detail's title was a `QLabel` and its
body a read-only `QTextBrowser`; design 4.3 planned `e` and 4.5 listed it under "not built". The
worker could already do it (`board_update` → `board_update_card` with `title` and
`replace_section`, hash-checked, the old text kept in the thread), so nothing new was needed on the
wire. **And `## Request` became `## Issue`** everywhere new text is written; both spellings are
read, and no card file was rewritten in bulk.

Design: `docs/SWITCHBOARD-DESIGN.md` 4.8. Protocol: `docs/AGENT-SESSIONS-PROTOCOL.md` 19.2–19.3.

## How this was run

`cmake --build` of this checkout into a scratch build directory, then `build/relay --fresh` under
this session's own **Xvfb `:183`** (1700x1000, no window manager), window 1600x950, with its own
`XDG_CONFIG_HOME` / `XDG_DATA_HOME` / `XDG_CACHE_HOME` / `XDG_RUNTIME_DIR` and `RELAY_KEYRING=off`,
so no provider key was used. Driven with `xdotool`, captured with `import -window`. The workspace
is a throwaway git repo holding a **copy** of this repository's `issues/` tree (138 files, 125 open
cards); the repository's own cards were never touched. `#HMN9` was created in that copy; `#KKYC` is
the copy's own card, edited there only.

## The shots

| File | Shows |
|---|---|
| `implementer-01-card-added-by-quick-add` | `n` and a line of text: `#HMN9` created in Discussing. On disk its section is `## Issue` — a new card never says Request. |
| `implementer-02-the-card-with-an-edit-affordance` | The card as it opens: the section renders as **Issue**, the header has **Edit**, and the key line says `e edit the title and issue`. |
| `implementer-03-e-opens-the-title-and-issue-fields` | `e`: the title becomes a field, the document becomes the issue editor ("Issue — Ctrl+Enter saves, Esc cancels") with **Cancel** / **Save**, the reply box stands down and **Edit** greys out. |
| `implementer-04-both-retyped` | A new title typed in the field and a new issue text in the editor. |
| `implementer-05-saved-and-the-thread-keeps-the-old-text` | Ctrl+Enter: "Saved #HMN9 · Undo", the card is read-only again with the new title and text, and the thread holds the update event plus a `rewrite` entry with *before* and *after* for both. On disk: `# Clickable paths in the terminal output` and `## Issue` with the new words. |
| `implementer-06-survives-a-restart` | The app quit and started again; `#HMN9` reopens with the edit in place — the file is the record. |
| `implementer-07-the-file-changed-under-the-edit` | Mid-edit, the card file is rewritten from a shell. The editor keeps every word that was typed. |
| `implementer-08-the-save-is-refused-nothing-lost` | Save then answers `board_conflict`: nothing was written (the shell's line is still the only one in the file), the card was re-read for its new hash, the typed text is still there and the line says "This card changed on disk … Save writes your text over that version (the thread keeps the old one); Esc drops your edit." A second Save wrote it, and the thread's `rewrite` entry holds the version it replaced. |
| `implementer-09-a-card-still-headed-request` | `#KKYC`, filed before today, still says `## Request` and reads normally. |
| `implementer-10-edit-button-and-the-next-time-e-hint` | The **Edit** button opens the same editor, seeded with that card's Request text and labelled Issue — and the shortcut hint fires: "Next time: e". |
| `implementer-11-saved-as-issue` | Saving it settled the card on `## Issue` (heading renamed, text kept, the surrounding front matter and title untouched) with the rewrite logged. |

## Checks that are not pictures

- `ctest -R board`: 35 cases, including two new ones — the editor's round trip (`board_update` with
  `base_hash` and `{title, replace_section: {heading: "Issue"}}`, Esc writing nothing, the card
  going back to being read on `board_written`) and the conflict path.
- `tests/test_board.py`, `tests/test_board_tools.py`: a new card writes `## Issue`; a card headed
  `## Request` is read through `board_read`'s `issue`/`issue_heading` and settles on `## Issue` when
  edited; `replace_section {heading: "Request"}` from an older caller edits the same section.
- The repository's own `issues/` tree was not rewritten: `grep -c '^## Request'` over it is
  unchanged by this work.
