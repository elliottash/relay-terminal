---
id: FDBK
type: work
status: inbox
labels: [feature, sessions, remote]
assignee: unassigned
rank: zzzzzzzzzzzzzzzzs
created: '2026-09-21'
source: 'Claude Code in a Relay pane, 2026-09-21'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# /bug (and /feedback): save the session and send it to a clearinghouse

## Issue
add a /bug command (alias /feedback) that will save the session and send to a bug / feedback clearinghouse

## Discussion points
Raised at intake, not decided — the owner's calls before this is planned:

- **Where the clearinghouse is.** The gateway on the Hetzner box (`gateway/`, api.relay-terminal.ai)
  is the only endpoint Relay already talks to with an identity; the alternatives are a public
  GitHub issue, email, or a Switchboard card on a board the project owns. Which one decides
  whether a report needs an account, and whether anyone outside Relay can read it.
- **What "save the session" sends.** `/export` (`Pane::slashCommands()`, src/Pane.h:8438) already
  writes the conversation as Markdown, but a session also holds cwd, file contents, terminal
  scrollback, tool output and env — anything pasted into it. Uploading that unedited is a
  disclosure; whether it is redacted, whether the user sees a diff of what is about to leave, and
  whether attaching the transcript is opt-in per report.
- **Whether the user reviews before it sends.** A one-key `/bug it crashed` that uploads silently
  is the useful shape; a preview pane is the safe one. Probably: compose in a pane, show exactly
  what is attached, one button to send.
- **What comes back.** A report with no id or reply path is a dead end. If it lands on this
  project's Switchboard as a card, the reporter can be given `#ID`.
- **Whether the same command serves Relay's own users and this repo.** Feedback about Relay from a
  shipped build has nowhere to go today; `/card` files into the *project's* board, which is not
  the same thing.

## Planning notes
Neighbours in the code, for whoever plans it:

- `Pane::slashCommands()` (src/Pane.h:8438) is the built-in list; `/export` and `/card` are the
  two commands closest in shape to this one.
- `relay::slash` (src/SlashCommands.h) decides what counts as a command attempt and what an
  unknown name answers — an alias like `/feedback` is a second entry in the list, not a special
  case.
- `backend/relay_core/guest_slash.py` holds the names guest harnesses already answer to;
  `/bug` is not among them, so it is free.
