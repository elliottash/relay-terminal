---
id: QVVA
type: work
status: needs-qa-llm
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: implemented by Claude Opus 5 (GUI F1 worker), 2026-09-17
rank: yu
created: '2026-09-17'
acceptance: a non-Claude model QA session on a real desktop runs the checklist and records it under `docs/qa_evidence/`
source: owner decisions (intake batch 2) relayed by the coordinator; `docs/AGENT-SESSIONS-PROTOCOL.md` section 11
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Thinking stream, "✦ thought for N s", "✦ N tool calls" links and the relay:// handler

## Behavior as implemented

- `thinking_delta` streams dim italic text into a floating "Thinking… · model" panel over the bottom of the terminal (Actions › Agent options › Show thinking, default on; × hides it for the turn). It floats instead of taking layout space because resizing the terminal made the idle shell redraw its prompt in the middle of the inline output. `thinking_done` hides it and prints `✦ thought for N s` (the line prints even with Show thinking off; skipped for `chars: 0`).
- `turn_summary` for a turn with tools prints `✦ N tool calls · T s  (Ctrl+click)` as an OSC 8 link to `relay://turn/<pane token>/<turn id>`. While a program owns the terminal it prints plain text pointing to Actions › Open last agent turn.
- Links: `Relay.profile` adds `AllowEscapedLinks=true`, `EscapedLinksSchema=…;relay://`; the pane re-applies its profile after starting the shell (KonsolePart applies it before the view exists, which left OSC 8 disabled). On first run Relay writes `~/.local/share/applications/org.relayterminal.Relay.url-handler.desktop` (template `data/org.relayterminal.Relay.url-handler.desktop`), runs `xdg-mime default`, `update-desktop-database`, `kbuildsycoca5` (idempotent, one status message). `scripts/relay-open` forwards `relay://` URLs over the open socket (address also published in `$XDG_RUNTIME_DIR/relay/open-socket`).
- The turn pane (`src/TurnTranscript.*`) lists tool calls (✓/✗, preview, exit code; expand for detail) and the turn transcript; Enter or "Open output" sends `tool_output_get` and opens the stored output as a 0600 temp file (`.diff`/`.log`) in a preview pane. The live `tool_output {text}` stream is distinguished from the stored reply by `stored: true`.

## Implementer check (not a QA verdict)

Xvfb, Kimi K3, main-tree backend (`docs/qa_evidence/2026-09-17-thinking-tool-summaries/`): reasoning streamed in the panel (`implementer-02`); the terminal showed "✦ thought for 7 s", tool lines, the answer and "✦ 2 tool calls · 13 s" (`implementer-01`); Ctrl+click went through KIO → the registered desktop handler → relay-open → socket and opened the turn pane (`implementer-03`); Enter on read_file opened its stored output (`implementer-04`).

## Known gaps

- A later terminal resize (for example opening the output preview beside the pane) makes Readline redraw its prompt and can overwrite the last inline line, including the link line. Pre-existing for all inline output.
- On desktops where KIO is not used or `xdg-mime` is missing, links do nothing; Actions › Open last agent turn still works.
- The handler entry points at the helper path of the Relay that registered it; a moved install re-registers on next start.

## QA checklist

1. Ask for a task with two tool calls; watch the thinking panel; check the two summary lines.
2. Ctrl+click "✦ N tool calls": a turn pane opens beside the pane; Enter on each call opens its output.
3. Turn Show thinking off: no panel, the "thought for" line still prints.
4. Run `vim`, then an agent prompt with tools: no terminal corruption; after exit use Actions › Open last agent turn.
5. `xdg-mime query default x-scheme-handler/relay` shows the Relay entry; restart Relay: no second status message.
