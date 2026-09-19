---
id: T4BS
type: work
status: needs-qa-llm
labels: [feature]
component: [gui, remote]
milestone: beta
workstream: remote
assignee: agent
implemented_by: Claude Opus 5 (Claude Code session relay-terminal-71), 2026-09-18
rank: '06'
created: '2026-09-18'
acceptance: a non-Claude model QA session runs the checklist with two isolated Relays under Xvfb (or the owner's desktop and sphinxpad) and records it under `docs/qa_evidence/2026-09-18-share-whole-tab/`
source: 'owner, 2026-09-18: "also for share, add an option, share whole tab, so then the partner gets access to all panes in the tab, so you can add more panes and they immediately get access to the workspace"'
links: {plans: [], commits: [97b17c0, 44bf6e1], evidence: ['docs/qa_evidence/2026-09-18-join-with-code-in-relay/'], related: [W5N2, 97EG, JQ7R], github: null}
---
# Share whole tab: a partner gets every pane in the tab, and each pane added to it

## Issue

Sharing was per pane. The owner wants to share a tab, so that a partner gets every pane in it and
every pane the owner adds to it later, without making a second invite.

## What landed (97b17c0)

- The share dialog has **"Share the whole tab"**. Ticked, every terminal in the tab is shared under
  the tab page's id (`relayShareTab` property, `RelayWindow::shareTabId`), and
  `RelayWindow::syncTabShares` shares each pane later split off or moved in. The new pane's share
  chip lights and a toast says the tab's guests can see it. A pane moved out of the tab stops being
  shared. Unticking ends the tab's share.
- A link or meeting code made while it is ticked carries `tab`: `Invite.tab` / `Participant.tab`
  (`remote/guests.py`), and `Host.pane_tab` / `pane_gone` (`remote/host.py`) grow and shrink the
  scope **before** the filtered `panes` list goes out, audited as `scope_grown` / `scope_shrunk`
  first. A pane that leaves takes its control token and pending prompts with it; a guest left with
  no pane is removed and the invite burned. Ceiling: 32 panes.
- Unchanged: an invite made without the box stays exactly its panes; a guest of another tab gains
  nothing; a later pane's password prompt is refused to guests as the first one's is.
- The guest browser app (`app/guest.js`) takes its scope from the filtered `panes` list, so new
  panes appear as chips without a reload. A Relay that joined with `/join` (#JQ7R) opens them as
  panes in the joined tab.
- Docs: `docs/REMOTE-PROTOCOL.md` §10.1 (the tab as the unit of sharing) and §10.6 (audit kinds).

## Implementer evidence (not a QA verdict)

- `tests/test_remote_tab_share.py` (growth, shrink, close, ordinary invite unaffected, password
  refusal, store, sidecar ordering) and `GuestReachTests.test_a_guest_of_one_tab_never_reaches_another_tab_as_either_grows`
  in `tests/test_remote_security.py`.
- `docs/qa_evidence/2026-09-18-join-with-code-in-relay/implementer-live-04-*.png`: with a Python host
  sharing a tab, the pane added later opens in the joined Relay by itself.
- **Not yet driven live: the desktop half.** Nobody has watched a real Relay's dialog tick "Share
  the whole tab" and then auto-share a pane split off in that tab. That is checklist item 1–3.

## QA checklist

Two Relays, each in its own jail (see *Notes*), A sharing and B joining. Or the owner's desktop and
sphinxpad on the LAN.

1. On A, split one tab into two panes. Share one of them (the share chip under the prompt box), and in the dialog tick
   **Share the whole tab**. Both panes' share chips light; the dialog title says "Share this tab".
2. Make a meeting code (or a link) on A with the box ticked. On B, `/join CODE`, the PIN, Enter; on
   A admit the knock. B opens both panes in one new tab.
3. On A, split the tab again (Ctrl+Shift+Right or the pane's split). The new pane's chip lights and
   A shows a toast; **within a second or two** B shows the third pane in the same tab, and a guest
   browser on the same share shows a third chip.
4. On A, move a pane out of the tab (drag its header into another tab, or "Move to new tab"): it
   stops being shared (toast on A), B marks that pane "No longer shared with you." and receives no
   more of its output.
5. On A, close a pane in the tab: same as 4 for that pane. Close the last one: B's session ends.
6. Make a second, ordinary invite on A **without** the box, for one pane in the same tab; join from
   a browser. Split the tab on A: that guest does **not** gain the new pane.
7. On A, untick **Share the whole tab**: every pane shared by it stops being shared; B's session
   ends with a reason.
8. On A, `sudo -k; sudo true` in a pane added after the join: B is not offered the password field
   and a `secret_input` from B is refused (the security suite covers the wire; this is the UI).
9. Audit: `~/.local/share/relay/remote/audit-YYYY-MM.jsonl` in A's jail has `scope_grown` and
   `scope_shrunk` lines naming the tab, the pane and the participant, each **before** the matching
   `join`/`leave` effect.

## Notes for the QA session

- Set `RELAY_KEYRING=off` for every run and drive, or Relay overwrites the owner's real identity
  key in the keyring and every phone he paired has to pair again.
- Jail each Relay: fresh `HOME XDG_CONFIG_HOME XDG_DATA_HOME XDG_STATE_HOME XDG_CACHE_HOME TMPDIR`
  and a 0700 `XDG_RUNTIME_DIR`; launch with `--clean-shell --fresh`; pick a free X display. The
  pattern is in `docs/qa_evidence/2026-09-18-relay-to-relay/live.sh` and
  `docs/qa_evidence/2026-09-18-join-with-code-in-relay/live.sh`.
- For B to find A's code, point B's join dialog's **Server…** at A's address (A's share dialog
  shows it), or pick relay-terminal.ai on A.
