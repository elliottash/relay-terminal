<!-- relay:entry 20260921T022500Z-a1 author=claude-code kind=plan -->
### Claude Code · 2026-09-21 02:25
Filed from the owner's words: "the switchboard is the best thing we have to let a phone drive the
system. so build that now". Measured first: the Switchboard is unreachable from a phone (no
terminal view to share, no board view in `app/`, no `board_*` request accepted from a device, and
the Switchboard's own `BoardWorker` is invisible to the hub). Plan and the three-way contract are
in the card body. Claimed, executing: three subagents in parallel — the desktop bridge, the hub,
the phone view — then the drive, a deploy, and pairing the owner's iPhone and iPad with him.
