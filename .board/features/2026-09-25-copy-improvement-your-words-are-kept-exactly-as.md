---
id: DZX8
type: work
status: planned
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-25'
source: 'remote: iOS Safari'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Copy improvement “your words are kept exactly as typed”, remove that and search…

## Issue
Copy improvement “your words are kept exactly as typed”, remove that and search for other redundant text we can drop

## Done means
The New card sheet's request placeholder reads just "What do you want?" — the "Your words are kept exactly as typed" sentence is gone from `app/` — and every other user-facing string in `app/` that merely restates the verbatim policy or what is already visible on screen has been dropped or deliberately kept with a reason. Behaviour is untouched: the request is still stored verbatim and offline sends still leave the draft in the box. Failure looks like the old sentence still appearing anywhere in `app/` (or on a cached page), or a trimmed message that lost the actual information (the "Offline" reason, the format hints).

## Plan
**Goal** — Cut the "Your words are kept exactly as typed" sentence from the remote Board's New card sheet, and sweep `app/` for copy of the same kind: text that restates policy or what is already visible. No behaviour change.

**Findings** — The string is the placeholder of the New card sheet's request box, set in `app/board.js:1223` (`openCreateSheet()`): `'What do you want? Your words are kept exactly as typed.'`. The verbatim behaviour it describes is real and stays — see the comment at `board.js:1237` ("Verbatim: not trimmed inside, not reflowed") and `app/boardmd.js:21`. One sibling of the same flavour exists: the offline refusal at `board.js:1063` ends "…needs your desktop. Your words are still here." — but the return happens *before* `clearReply`, so the box visibly still holds the draft; the sentence restates the screen. Everything else checked and already terse: the Title/Labels/Why placeholders (`board.js:1216`, `1230`, `1170`) carry format hints, `board.js:1240`'s keyboard-mic note has a comment saying it is deliberate, `board.js:680`'s empty-board hint is an instruction, `QUEUED_WORDS` (`board.js:68`) is four words, the `aria-label`s are invisible, and the desktop's own Board placeholders (`src/BoardPane.cpp:1895`, `2163`, `6430`, …) are already bare. No test pins any of these strings.

**Steps**
1. `app/board.js:1223` — placeholder becomes `'What do you want?'`.
2. `app/board.js:1063` — drop ` Your words are still here.`, leaving `Offline — Plan/Discuss needs your desktop.` Keep the comment above the line (it explains the offline block, which stays).
3. Record the sweep: add nothing, but in the commit message list the strings checked and kept (Title/Labels/Why placeholders, mic note, empty-board hint, QUEUED_WORDS, aria-labels, desktop placeholders) so the reviewer can see the search was done, not skipped.
4. `node --check app/board.js`.

**Risks** — Step 2 is the only judgement call: it is the same reassurance pattern under a different word, and it is in scope for "other redundant text", but if the owner wants only the placeholder touched, step 2 is the one to drop. A stale cached page can show the old copy after the change — hard-refresh before judging.

**Verify** — `node --check app/board.js`; `rg -n "Your words|as typed" app/` returns only code comments; on the served page, hard-refresh and open the New card sheet to see the short placeholder, then go offline (desktop unreachable) and Send in Discuss mode to see `Offline — Discuss needs your desktop.` with the draft left in the box.
