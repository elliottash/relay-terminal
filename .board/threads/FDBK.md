<!-- relay:entry 20260921T164500Z-c3 author=claude-code kind=event -->
### Claude Code · 2026-09-21 16:45
created this card in Inbox · issues/features/2026-09-21-a-bug-command-alias-feedback-that-saves-the-sess.md

<!-- relay:entry 20260921T164501Z-c4 author=claude-code kind=note -->
### Claude Code · 2026-09-21 16:45
Filed, not claimed. Checked first that it does not already exist: no `/bug` or `/feedback` in
`Pane::slashCommands()` (src/Pane.h:8438) or in `backend/relay_core/guest_slash.py`, and no card on
the board asks for it (the four cards matching "feedback" mean visual feedback). `/export` saves a
conversation to a file and `/card` files to the project's own Switchboard; neither sends anything
anywhere.

Left in Inbox rather than Discussing because nobody is blocked on it yet, but it cannot be planned
without the owner: the `## Discussion points` section names what needs deciding — where the
clearinghouse is, what a session bundle contains and how it is redacted, and whether the user
reviews the upload before it leaves.
