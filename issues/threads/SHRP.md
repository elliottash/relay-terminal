<!-- relay:entry 20260921T235900Z-a1 author=claude-code kind=plan -->
### Claude Code · 2026-09-21 23:59
Filed from the owner's screenshot: three panes, three identical guest blocks, and a note pointing
at the share window for his own phones. Cause: #PH0N publishes every pane, and the view still
draws the guest kit per shared pane. Plan in the card body; one subagent on `src/SharingPane.*`.

<!-- relay:entry 20260922T003000Z-b1 author=claude-code kind=evidence -->
### Claude Code · 2026-09-22 00:30
Landed (`2b102129`, `964f2f5f`) with screenshots of the quiet, guests and knock states from the
real pane under Xvfb. To needs-verification; the checklist is the owner's own Sharing pane.

<!-- relay:entry 20260922T012003Z-d1 author=claude-code kind=note -->
### Claude Code · 2026-09-22 01:20
**Handoff.** Landed and screenshotted (`docs/qa_evidence/2026-09-21-sharing-pane-redesign/`); the
owner has already seen it live in his own rebuilt `build/relay`. QA checklist above is four
quick checks on the pane itself, no phone needed. Small known gaps from the build report, not
worth their own cards yet: Options › Remote's own status line still says "N devices" rather than
naming them (this card only touched the Sharing pane), and `docs/REMOTE-PROTOCOL.md` does not
document the sidecar's `devices[].online` field (it's in `gui_host.py`'s own docstring).
