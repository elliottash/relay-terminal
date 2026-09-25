# Staging notes — #GREM

This stages the **backend behaviour**, not the app: a `BoardTools` instance constructed exactly
as a terminal pane agent's is (no `console`, no `board_cleanup` running), over a throwaway
two-card board in `/home/elliott/.cache/relay/scratch/tryit/grem-merge`. Differences from real
use: the two cards are invented stand-ins for #BCJF / #P4XN, no model or session is behind the
tool calls (the script calls the tools directly, so nothing shows in a Relay pane's transcript),
and the board is a fresh fixture each run, not this repository's. The tool code is the live
`backend/relay_core/board_tools.py` from this checkout at commit a0041445b6d3.
