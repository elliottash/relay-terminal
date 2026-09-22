<!-- relay:entry 20260920T190200Z-a1 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 19:02
Filed and claimed from the owner's report and screenshot of a Switchboard card page. Reproduced
before touching anything, by driving `backend/worker.py` over NDJSON as the tab's helper
(`agent_role: "switchboard"`) on the owner's own preset, `guest:claude`: `configured` came back
with `guest: "claude"`, one guest process was started, and the first `board_chat` answered
`Base URL must be an HTTPS URL without credentials, query, or fragment`. Neighbour card #4NXH is
the same area but a different root (a guest *session* has no `board_*` tools; this is the helper
*worker* being built on a guest config).

<!-- relay:entry 20260920T190300Z-a2 author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 19:03
Evidence under `docs/qa_evidence/2026-09-20-helper-on-guest-main/`: the before transcript, the four
after transcripts (priority list, nothing usable, role pick, pane unchanged), and a live end-to-end
run in which a helper worker whose Main is `guest:claude` falls back to a loopback stub endpoint
and answers a question about the board. `NOTES.md` says how to re-run it; nothing calls a real
provider and no guest is ever started (`make_harness` is the seam, replaced by the test fake).

<!-- relay:entry 20260922T005810Z-6m author=agent kind=note pane=switchboard -->
Handoff: done, needs-verification. A helper never starts a guest harness (Claude Code/Codex) even when Main is one — it falls back to the Options > Models priority list and says so in its model box tooltip, or refuses with one plain sentence if nothing is usable. A pane on a guest preset is unaffected. QA checklist is a 6-item walk; nothing left to build.
