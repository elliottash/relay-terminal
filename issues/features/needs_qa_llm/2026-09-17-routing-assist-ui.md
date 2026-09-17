# Routing assist in the prompt box

- **Status**: needs-qa-llm
- **Component**: gui
- **Milestone**: desktop-alpha
- **Acceptance evidence**: a non-Claude model QA session on a real desktop runs the checklist and records it under `docs/qa_evidence/`
- **Assignee**: implemented by Claude Opus 5 (GUI F1 worker), 2026-09-17
- **Source**: owner decisions (intake batch 2) relayed by the coordinator; `docs/AGENT-SESSIONS-PROTOCOL.md` section 11
- **Workstream**: composer

## Behavior as implemented

- When a preview `route` has `needs_assist` (a command that is also an English word used in a sentence) and the mode is Auto, the label shows the local guess immediately and "AUTO · checking…" after 150 ms; 300 ms after typing stops Relay sends `route_assist {text, cwd, timeout_ms: 4000}` for the current text only. `route_assisted` updates the label, e.g. "AGENT · guessed: Natural language instruction to coding agent (100%)". Failure/timeout: "TERMINAL · local guess (model check unavailable: timeout) · ! or * to choose".
- Enter uses a cached answer for the same text; otherwise it waits at most 400 ms for the answer, then dispatches the local guess. Typing is never blocked. Prefixes, Ctrl+Enter and Ctrl+Shift+Enter never ask.

## Implementer check (not a QA verdict)

`docs/qa_evidence/2026-09-17-routing-assist/`: "echo hello to you all" showed checking with the local guess (`implementer-01`); with the pane's Kimi model and the old 2 s timeout the assist timed out and the local guess stayed (`implementer-02`, older label text); later "write the text hello again into notes.txt using write_file" showed "AGENT · guessed: … (100%)" (`implementer-03`).

## QA checklist

1. Type "install ripgrep" slowly and quickly: the label never flickers into a blocked state; typing stays responsive.
2. Type "make the tests pass", wait for "guessed", Enter: goes where the label says.
3. Type "go ahead" and press Enter immediately: routes within about half a second.
4. `*install ripgrep` and Ctrl+Enter: no assist request (check that the label does not say checking).
5. With a slow provider: the local guess is used and the label says the model check was unavailable.
