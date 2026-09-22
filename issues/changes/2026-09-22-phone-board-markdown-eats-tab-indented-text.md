---
id: MDX6
type: work
status: inbox
labels: [bug, remote, switchboard]
assignee: null
rank: zmdx6
created: '2026-09-22'
source: 'Measured by Claude Code rendering card bodies through app/boardmd.js in Chrome, 2026-09-22'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-22-phone-ux-drive/'], related: [SWPH, PBXC], github: null}
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
