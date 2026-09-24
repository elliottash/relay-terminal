---
id: Q4SD
type: work
status: needs-qa-llm
labels: [change, bug]
component: [gui]
milestone: desktop-alpha
workstream: routing
assignee: agent
implemented_by: Claude Opus 5 (Claude Code, agent B), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'An unknown `/command` prints Relay''s own line with the closest real commands and `/help`; a real slash command still runs; `/usr/bin/foo` and `/tmp` still run in the shell; `./scripts/test.sh` (883) and `ctest` (29 groups, including the new `slash` group) pass'
source: 'owner, bug intake 2026-09-18: "if / commands are not found, it says ''/command not found''"'
links: {plans: [], commits: [95f05b4], evidence: ['docs/qa_evidence/2026-09-18-unknown-slash-command/'], related: [G8DK], github: null}
---
# An unknown `/command` is answered by Relay, not by the shell

## Report

Typing a slash command Relay does not have — `/nosuchthing`, or a typo like `/comapct` — came
back as `bash: /nosuchthing: command not found`. The line had been offered to the built-in
commands and to the aliases, both declined, and then it fell into the router like any other text:
`_resolve` reads a word with a `/` in it as a path, so the line went to the agent with
`no such file: /nosuchthing` under it, and in terminal mode it went to Bash. Either way the shell
answered for a command the shell never owned, and nothing told the user what Relay does have.

## Change

A submission is still offered to `tryRunSlashCommand` and then to `tryRunAliasSlash`. A
`/command` that is neither now stops there (`Pane::reportUnknownSlashCommand`) and Relay answers:

```
✗ Unknown command: /comapct · did you mean /compact? · type / for every command, /help for the keys
```

- **What counts as an attempt.** `relay::slash::attemptedName` (`src/SlashCommands.*`): one
  name-shaped word after the `/` (a letter, then letters, digits, `-` or `_`; at most 40
  characters), on a single line, that is not a path. A second `/` in the word (`/usr/bin/foo`,
  `/etc/hosts`), a first word that is not a name (`/2fast`, `/*.txt`), `/` alone, or a
  single-segment path that exists on this machine (`/tmp`, `/bin`) all fall through to the router
  exactly as before. The filesystem check is injectable, so the rules are testable anywhere.
- **The suggestions.** `relay::slash::closest` ranks the built-ins and this window's aliases (the
  same list `/name` runs, issue G8DK) by optimal string alignment distance, so a transposition
  counts as the one slip it is: `/comapct` → `/compact`, `/deplyo` → an alias `deploy`. A name
  the user was part way through typing (`/conv`) ranks first. One slip is allowed for a name of
  four characters or fewer, two above that, and at most three names are named. Nothing close
  enough means no guess — `/nosuchthing` gets the line without one, because a wrong guess reads
  worse than none.
- **`/help`.** The line points at something that exists: `/help` shows the card `?` already shows
  in an empty prompt box (`toggleHelpCard`), and typing the slow form fires the shortcut hint the
  standing rule in `WARP.md` asks for ("Next time: press ? in an empty prompt box"). It shows the
  card rather than toggling it, so typing `/help` while the card is up leaves it up.
- **Before Enter.** The route preview for an unknown name says the same thing
  (`COMMAND · /foo · not a Relay command · did you mean /fork?`), alongside the existing preview
  for a known one.
- `/shell ` and `/agent ` are names in the same list, so they keep falling through to the router
  and keep their prefix hints.
- The router, `explain_invalid` and `Pane::dispatch` are untouched: the "command not found:
  symlink" rule from the other 2026-09-18 owner report is not in this path and is not affected.

New files: `src/SlashCommands.h`, `src/SlashCommands.cpp` (library `relay-slash`),
`tests/slashcommands_test.cpp` (ctest group `slash`). `src/main.cpp` gained the `/help` entry,
`Pane::slashNames()`, `Pane::reportUnknownSlashCommand()` and the two call sites in
`Pane::requestRoute`. Documented in `docs/ARCHITECTURE.md` (section 5 "Slash commands", the
shortcut-hint trigger list, the file map) and `README.md`.

## QA checklist

1. **The unknown command.** In a pane, submit `/nosuchthing`: one red `✗ Unknown command:
   /nosuchthing · type / for every command, /help for the keys` line. Nothing runs in the shell,
   no prompt goes to the agent (the agent may be unconfigured and this must still work), and the
   composer is cleared. Up-arrow brings the line back from the prompt history.
2. **The suggestion.** `/comapct`, `/moldel`, `/contiune`, `/rewnid` each suggest the real command.
   `/conv` suggests `/conversations`. `/NEW` suggests `/new`. `/xyzzy` suggests nothing and still
   prints the line.
3. **An alias.** Define an alias (say `deploy`), then submit `/deplyo`: the alias is suggested,
   because aliases are in the same list. `/deploy` itself still runs the alias.
4. **Real commands.** `/new`, `/light`, `/dark`, `/help`, `/tasks`, `/compact focus`, `/model`
   still do what they did. `/shell echo hi` runs in the terminal and `/agent hello` goes to the
   agent, both with their prefix hints.
5. **Paths are still the shell's.** `/bin/echo relay-shell-ok` runs and prints. `/usr/bin/env`,
   `/etc/hosts` (fails in the shell, as it should) and `ls /tmp` behave as before. `/tmp` on its
   own is a path, not a command — it must not produce Relay's line. A directory that does *not*
   exist and is not name-shaped, `/nosuchdir/nosuchthing`, also still goes to the shell path.
6. **Every mode.** The same line appears in auto mode, in terminal mode (`!` or Ctrl+Shift+Enter)
   and in agent mode (`*` or Ctrl+Enter) — a slash command is Relay's in every mode, as `/new`
   always was. Check this is the wanted rule: `/shell /nosuchthing` is the way to insist on Bash.
7. **`/help`.** `/help` shows the same card `?` shows, and a toast reads "Next time: press ? in an
   empty prompt box". With the card already up, `/help` leaves it up. Turning "Shortcut hints" off
   in Settings silences the toast and nothing else.
8. **The popup.** Type `/`: `/help` is listed with its description. Typing `/nosuch…` closes the
   popup (nothing matches) and Enter then prints the line rather than completing anything.
9. **Nothing regressed in routing.** A plain request ("symlink from ~/projects to here") still
   goes to the agent with no "command not found: symlink" note under it, and a mistyped command
   ("gti status") still keeps its note.
10. **Tests.** `ctest --test-dir build` (29 groups, including `slash`) and `./scripts/test.sh`
    (883) pass. Both were re-run green on main after the code landed in 95f05b4.

## Known gaps

- The route preview reaches the mode chip's tooltip, which Qt rebuilds on the next picker
  refresh; for an unknown command, as for a known one, there is no visible label before Enter.
  Making the preview visible would mean refreshing the pickers on every keystroke, which touches
  the model picker and was left alone.
- Suggestions are lexical only. `/undo` does not suggest `/rewind`, because nothing maps intent
  to commands; the palette's hidden alias words (`paletteAliases()`) are the place for that if it
  is ever wanted.
- `/help` shows the short card. The full list stays behind Ctrl+? (Settings › Actions).
