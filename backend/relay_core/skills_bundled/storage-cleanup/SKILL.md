---
name: storage-cleanup
description: See how much disk Relay's own records use (pane text journals, saved layouts, conversation stores, the index) and clear old records at the user's explicit instruction. Report sizes first; delete nothing on your own initiative.
---

# Storage cleanup

The owner, 2026-09-25: "show disk space in options and then a helper agent has a skill to help
you clean out large records." Options › Storage is the report; this skill is the broom. The
report always comes first — the user decides what, if anything, is forgotten.

## Where Relay's records live

Everything is under the data root, `$XDG_DATA_HOME/relay` (usually `~/.local/share/relay`):

- `text/<scrollback-id>/` — the pane text journals (card #HEY7), one folder per pane that ever
  opened. This is almost always the large area.
- `state/` — `windows.json`, scrollback snapshots, prompt history: what Relay restores windows
  from. Small; leave it alone.
- `sessions/<workspace-digest>/` — the conversation/session store, one folder per workspace;
  guest sessions live under `sessions/guests/`.
- `index.db` — the SQLite conversation index. A cache: Relay rebuilds it from `sessions/` when
  it is missing or its schema version changed, so never "clean" sessions by hand-editing the
  index.

## Look, then report

```bash
python3 -S backend/relay_core/textjournal.py du          # from the repo root
```

prints one JSON object: the journal root, the total bytes and lines, and a `journals` list
with each pane's id, bytes, lines, cwd and last-written time — the same store Options ›
Storage reports as "Pane text journals". For the other areas a plain
`du -sh ~/.local/share/relay/*` is fine.
Show the user these figures and name the horizon you propose BEFORE deleting anything. Never
present a cleanup as already decided.

## What maintains itself (do not redo it)

Journals compress themselves: a journal idle for an hour seals to zlib, and sealed segments
older than 7 days recompress to xz (relay_core/textjournal.py, `SEAL_IDLE_SECONDS`,
`RECOMPRESS_AFTER_SECONDS`). The CLI exposes the same passes when you want them on demand:

```bash
python3 -S backend/relay_core/textjournal.py seal-idle   # seal journals idle past the threshold
python3 -S backend/relay_core/textjournal.py recompress  # recompress old sealed segments to xz
```

Neither deletes anything; both are safe to run while Relay is up.

The conversation index also heals itself: `conv_index.reconcile()` drops rows whose session
files went away, so deleted sessions stop appearing in search on their own.

## Deleting — only on the user's explicit instruction

`textjournal.forget_older_than(seconds, root=..., keep=...)` is the only bulk delete: it
removes journal folders whose newest segment is older than the horizon (seconds, not days)
and returns how many went. It is a library function, not a CLI subcommand, so call it as one:

```bash
python3 - <<'EOF'
import sys; sys.path.insert(0, "backend")
from relay_core import textjournal
gone = textjournal.forget_older_than(90 * 86400)   # journals untouched for 90 days
print(f"forgot {gone} journal(s)")
EOF
```

`keep` is the set of scrollback ids to spare no matter their age; pass the ids of panes the
user cares about when they name any.

Rules, no exceptions:

1. Run `du` first and show the user what the horizon would free: the journals it deletes are
   the ones idle past the horizon, which is an age cut, not a size cut — say which and how
   much, don't just wave at the total.
2. Get an explicit "yes, forget journals older than N days" from the user. A vague "clean
   things up" is not one — propose the horizon and wait.
3. A forgotten pane keeps its newest rows (the tail in `state/scrollback/` and the terminal's
   own ring); everything older that only the journal held is gone for good. Say so when you
   propose the horizon.
4. After forgetting, re-run `du` and report the new total.

Conversation records in `sessions/` are different: Relay keeps a session's files until the
session is deleted (sessions.py `Store.delete`, what the Sessions manager calls), and there is
no time-based expiry to tighten. Do not delete files under `sessions/` yourself — if the user
wants conversations gone, point them at the Sessions manager, and let the index's reconcile
drop the rows.
