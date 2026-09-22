---
id: MDX6
type: work
status: needs-verification
labels: [bug, remote, switchboard]
assignee: claude-code
implemented_by: anthropic/claude-opus-5 via claude-code
rank: zmdx6
created: '2026-09-22'
source: Measured by Claude Code rendering card bodies through app/boardmd.js in Chrome, 2026-09-22
links: {plans: [], commits: [25067cb8], evidence: [docs/qa_evidence/2026-09-22-phone-ux-drive/, docs/qa_evidence/2026-09-22-streamC-board/], related: [SWPH, PBXC], github: null}
---
# The phone Board's Markdown eats tab-indented text and bolds `__init__`

## Issue
`indentOf()` measures a tab as four columns
(`app/boardmd.js:281`: `line.match(/^\s*/)[0].replace(/\t/g, '    ').length`) and both call sites
then slice by **character** count — `line.slice(Math.min(indentOf(line), base + 2))` at
`app/boardmd.js:320` (list continuation) and `:349` (a fence inside an indented block). On a
tab-indented line the slice eats the tab *and* real text.

Rendered through the real `renderMarkdown` in Chrome
(`docs/qa_evidence/2026-09-22-phone-ux-drive/`):

| card body | what the phone shows |
|---|---|
| `- run this:` / `\tmake all` | `run this:` / **`ake all`** |
| `- a` / `\t- b` | `a` / `b` — the nested bullet flattened, the `-` eaten |
| `the __init__ method and a__b__c` | `the `**init**` method and a`**b**`c` |
| `Results:` then a pipe table with no blank line before it | four literal `|`-lines |

The first two are the serious ones: this is the one place in the app that promises the owner's text
is reproduced exactly, and a build command that is wrong by one character is worse than no command.
The dunder case is `app/boardmd.js:161` — the `**`/`__`/`~~` branch has no intraword guard, while
the single-`_` branch below it has one ("an underscore opens only at a word's edge"); CommonMark
and GitHub both leave `__init__` alone, and this repository's cards name Python dunders routinely.
The table case is the paragraph loop's exit conditions (`app/boardmd.js:~395`) not listing
`TABLE_RULE`; GitHub renders that as a table.

No injection was found: there is no `innerHTML`, `DOMParser` or `template` in `app/board.js` or
`app/boardmd.js`, `<script>` and `javascript:` hrefs in a card body render as text, and `OPENABLE`
gates both the parse site and the Open button.

## Done means
A card body renders on the phone character for character where it is not marked up: a
tab-indented continuation line keeps its first character, a tab-indented nested bullet stays a
bullet, `__init__` stays `__init__`, and a pipe table directly under a paragraph line is a table.
It fails if any line of a card body loses a character to the renderer.

## Execution Summary
`indentOf()` measured an indent in columns, counting a tab as four, and both call sites then cut
that many *characters* off the front of the line — so on a tab-indented line the cut took the tab
and the text behind it. Measure and cut now agree: `indentWidth(space)` is the one place a tab is
worth four columns, and `stripIndent(line, columns)` walks the leading whitespace in columns,
leaving a tab that straddles the cut as its remaining columns of spaces. Both call sites use it —
the list continuation (`app/boardmd.js:359`) and a fence inside an indented block (`:388`, which
had been mixing a column measure with `fence[1].length`, a character count).

`__init__` was read as bold. `opensStrong()` now gates the `__` branch: `**` and `~~` are
unchanged, and `__` opens bold only outside a word and only where what it wraps is not a bare
identifier. `__also bold__` is still bold; `__init__`, `__main__`, `__all__` and `a__b__c` are the
characters the owner typed. It is written up as the fourth deliberate departure from Markdown's
defaults at the top of the file, beside the three that were already there.

A pipe table written straight under a sentence, with no blank line, was four literal `|`-lines.
`startsTable(lines, at)` is the block pass's own test, factored out, and the paragraph loop now
stops at it — which is what GitHub does.

`app/board.css` was claimed for this stream and needed no change.

## Tests
`RELAY_KEYRING=off python3 -m unittest tests.test_board_view` — 32 tests, OK, 22 s (26 that were
already there, 6 new across this card and #RCN8). Re-run by the orchestrating session after the
landing.

- `tests/test_board_view.py::BoardViewTests::test_a_card_body_reaches_the_reader_character_for_character`
  — eight card bodies through the real `renderMarkdown` in headless Chrome, turned back into lines
  and compared character for character: a tab-indented `make all`, one indented with two tabs and
  one with spaces-then-tab, a tab-indented nested bullet, a numbered item's tab-indented
  continuation, two dunder sentences, and a table under a paragraph line. It then checks what each
  *became* — one list and no nesting; a nested `li`; `pre` text `b` and `make all`; no
  `strong`/`em` for `__init__`; `strong` for `__also bold__`; the table's cells.
- `manual: docs/qa_evidence/2026-09-22-streamC-board/markdown-before-after.md` — `render_drive.py`
  loads `458582b2`'s renderer and the current one into one browser from one origin and draws the
  same strings through each. Six of seven bodies came out differently.
- `manual: docs/qa_evidence/2026-09-22-phone-ux-drive/` — the probe this card was filed from,
  re-run unchanged against the landed tree by the orchestrating session: `\tmake all` → `make all`
  (was `ake all`); `- a` / `\t- b` → a real nested `<ul>` (was flattened, the `-` eaten);
  `the __init__ method and a__b__c are identifiers` → itself, no `<strong>`; the pipe table under a
  sentence → a real `<table>` with its cells.
