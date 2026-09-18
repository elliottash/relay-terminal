# An unknown `/command` is answered by Relay, not by the shell (2026-09-18)

The owner's report: "if / commands are not found, it says '/command not found'". A `/command`
Relay does not have fell through the slash-command registry into the router, which sent it on as
a prompt (or, in terminal mode, into Bash), and the answer came back as
`bash: /nosuchthing: command not found` — the shell answering for a command it never owned.

Implementer screenshots (`implementer-` prefix), not a QA verdict. `drive.sh` reproduces them
under Xvfb `:187` with an isolated `XDG_CONFIG_HOME` on a throwaway workspace; this repository is
never opened, no provider is configured and nothing is ever sent to a model.

| Shot | What it shows |
|---|---|
| `implementer-00-preview-composer.png` | `/comapct` typed, not yet submitted. The route text for an unknown command travels the way a known one's does — into the mode chip's tooltip, which is rebuilt on the next picker refresh — so the preview is not a visible label here. |
| `implementer-01-unknown-with-suggestion.png` | Enter: `✗ Unknown command: /comapct · did you mean /compact? · type / for every command, /help for the keys`. Nothing reached the shell or the agent. |
| `implementer-02-unknown-no-suggestion.png` | `/nosuchthing`: the same line with no guess, because nothing in the registry is within two slips. A wrong guess would be worse than none. |
| `implementer-02b-both-lines-detail.png` | Both lines together, cropped. |
| `implementer-03-help-card.png` | `/help`, the command the line points at: the card `?` shows in an empty prompt box, and the shortcut hint the standing rule asks for ("Next time: press ? in an empty prompt box"). |
| `implementer-04-real-command-runs.png` | A real slash command still runs: `/light` switches the theme to IBM Beige. |
| `implementer-05a-path-preview-is-terminal.png` | `/bin/echo relay-shell-ok` in the composer: highlighted as a shell command, not as a slash command. |
| `implementer-05-absolute-path-runs-in-shell.png` | Enter: it runs in the shell and prints `relay-shell-ok`. An absolute path is still the shell's. |
| `implementer-06-terminal-mode-same-line.png` | `!` then `/nosuchthing`: terminal mode gets the same line. A slash command is Relay's in every mode, as `/new` always was; `/shell /nosuchthing` is the way to insist on Bash. |
| `implementer-07-existing-path-not-intercepted.png` | `/tmp`, a single-segment path that exists: Relay's line is absent and the router answers as it always did (`is a directory: /tmp`, and with no provider configured, "No agent provider is configured to fix it"). |

`implementer-relay-stderr.log` is the app's stderr for the run (empty).

Unit coverage for the same rules, without a window: `tests/slashcommands_test.cpp`
(`ctest -R slash`).
