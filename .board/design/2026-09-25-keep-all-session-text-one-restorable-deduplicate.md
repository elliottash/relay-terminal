---
id: HEY7
type: work
status: executing
labels: [feature, sessions, terminal, design]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
rank: zzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [person], human: optional, criteria: a shell pane with >5000 coloured lines closed and reopened from Recently closed shows every line with its colours, sign_off: none, effort: high, stakes: rework, blast: capability}
links: {plans: [], commits: [c0a8e78f38f9, 2253e86971e5], evidence: [], related: [PJ8K, RC7Z, 5A37], github: null}
---
# Keep all session text: one restorable, deduplicated, compressed text journal

## Issue
Save every pane's text by default, shell output included, in a form that restores with its formatting. Store each piece once: agent output comes back from the transcript, and only shell output is stored as text. Keep formatting as a separate style layer that is applied again on restore. Compress sealed text after a delay. Session search can include shell output, but only behind a flag that is off by default.

> i think relay should save all session text by default. claude and codex do that right? when you say terminal text, is that duplicative of session text?
>
> help me design this so that 
>
> 1) all text is saved in a restorable form. formatting should be restorable, but does not need to have it applied in the saved text. it can be added back on restore. 
> 2) no duplication
> 3) session search can include shell output but its a flag that is off by default. 
> 4) saved texts should be compressed to the extent possible, potentially after some delay.
> — elliott · [session:9d5cb98a7f3147eeb1df3f422e8cd700](relay://session/9d5cb98a7f3147eeb1df3f422e8cd700) · 2026-09-25

## Plan
**Goal.** Every line a pane showed can be brought back with its formatting, each piece of text is stored exactly once, and what is stored is compressed.

**Findings (2026-09-25, measured on this machine).**
- Today the same text is kept up to four times. (1) A pane holding a conversation writes its terminal text twice: the per-pane `state/scrollback/<id>.txt` (`Pane::saveScrollback`, `src/Pane.h:1499`) and the per-session `<session>.scrollback.txt` (`saveSessionText`). (2) The session sidecar re-renders what the transcript `<id>.json` already holds: one session's sidecar is 204 KB against 80 KB of transcript. (3) `index.db` (198 MB) stores every entry's text in `entries` as well as in FTS5, including the sidecars as `terminal_text` rows (`conv_index.sidecar_entries`). (4) Rewinds keep `<id>.rewound-<n>.scrollback.txt` next to `rewound.jsonl`, which already holds the dropped messages.
- The text is capped: 5000 lines / 512 KB per file (`kScrollbackMaxLines`, `kScrollbackMaxBytes`). A shell pane's text is pruned once the pane leaves the layout and Recently closed. Commands typed straight into a shell are never indexed (only commands Relay itself ran reach the `terminal` source).
- Relay can already redraw a conversation from its transcript: `src/TranscriptReplay.h` (#0TJ9, #KDB4) is the fallback when no sidecar exists. Today it drops tool output.
- Compression, measured: 842 sidecars, 15.5 MB → 1.65 MB with `zstd -19` (9.4×). The largest transcript is 5.7 MB → 316 KB (18×). `zstd -3` gets about 85% of that at a small fraction of the CPU. Taking the SGR codes out of a text file saves only ~8% of its raw size, so a separate style layer is for cleanliness and searchability, not for size.
- Claude Code deletes its own transcripts after `cleanupPeriodDays` (default 30; not set here). Codex keeps its rollouts. A reference to a Claude transcript therefore goes stale after a month.

**Design.**
1. **One text journal per pane lifetime** (`<data>/relay/text/<yyyy-mm>/<journal-id>.rtj`). It is an ordered list of segments, and nothing is stored twice:
   - `shell`: text the pty printed that exists nowhere else. It is stored as **plain UTF-8 lines** plus a **style layer**: per line, runs of `(column, length, style#)`, an interned style table (bold/italic/underline, indexed or RGB fg/bg as emitted), OSC 8 link targets (image/media rows), and the prose blocks (#MTCS). Command boundaries (OSC 133 prompt / command / exit status) are kept, so search and restore can work per command.
   - `conversation`: a *reference*, `{source: relay|claude|codex, session_id, from_turn, to_turn}`, and no text. It restores through the transcript renderer.
   - `note`: Relay's own one-line chrome (status and error lines that no transcript holds). Tiny.
2. **Capture lines as they become final.** A line is appended when it scrolls from the screen into history, rather than by snapshotting the buffer every so often. The journal then holds everything, not the last 5000 lines. The visible screen is the live tail, written on save and close. While a conversation owns the pane, its output becomes one `conversation` segment rather than lines. A guest running as a TUI in the pty (Tier B) is recorded the same way, because its transcript holds the same content.
3. **Restore** walks the segments. `shell` segments get SGR/OSC 8 regenerated from the style layer and are replayed through the existing restorable-ANSI filter. `conversation` segments are drawn by `TranscriptReplay`, upgraded to live-turn fidelity (tool output folded under its ▸ row, as a live turn shows it). Formatting is therefore restorable but never baked into the stored text.
4. **Search.** Shell segments are indexed per command (prompt line + output chunk) as a new source, `shell`, in an FTS5 table **without its own copy of the text** (contentless, `contentless_delete=1`). Snippets are cut from the journal on a hit. Sessions leaves `shell` out unless the flag is on: an Options setting, *Search shell output* (off by default), plus a Sessions filter check box and a `has:shell` operator for a one-off search. Indexing always runs, so turning the flag on is instant. A shell hit opens as a read-only replay of that journal, in a new pane with a fresh shell in the same directory.
5. **Compression.** The live tail is a plain append file, cheap and safe if Relay crashes. A segment is **sealed** when its pane closes, after 10 minutes idle, or at 1 MB. Sealing writes one `zstd -3` frame. After 7 days a background pass rewrites sealed frames at `zstd -19 --long`, optionally with a dictionary trained on the user's own journals. Readers take both. The same delayed pass compresses Relay session JSON that has been idle for 7 days (`<id>.json.zst`; `SessionStore` reads either). Existing `.scrollback.txt` sidecars are compressed in place and stay readable. They are never converted into references, because the split between agent and shell text cannot be recovered from them.
6. **Removing the duplicates.** Stop writing the per-session sidecar and the rewound sidecars for new text; the transcript plus the journal's `conversation` segment replace them. The per-pane `state/scrollback` store becomes a pointer to the pane's journal. Recently closed records (#PJ8K) keep a journal id instead of a scrollback id. Old sidecars stay readable until they are deleted.
7. **Guest transcripts that the guest deletes.** Because of Claude's 30-day cleanup, Relay copies a guest transcript into its own store, compressed, when that transcript is referenced by a journal and older than 21 days. The copy exists only once the original is about to go. (Decision below.)

**Steps.**
1. `src/TextJournal.{h,cpp}` (QtCore, its own library) plus `backend/relay_core/textjournal.py` reader: the format, append/seal/read, and the SGR ↔ style-layer round trip. Tests: every SGR form the engine emits survives the round trip; a crash mid-append loses at most the last line.
2. Engine hook: a line committed to history → `Pane` appends it to the journal. The pane's conversation boundaries (`adoptSessionText`) open and close `conversation` segments.
3. Restore from journals, for both saved layouts and Recently closed. `TranscriptReplay` redraws tool output folded, as live turns do.
4. Stop the duplicate writers (per-session and rewound sidecars); migrate the per-pane store.
5. Index: `shell` source, contentless FTS table, the Options flag, the Sessions check box, `has:shell`, snippet from the journal, and open-a-hit.
6. Compression passes: seal on close / idle / size; recompress at 7 days; compress idle session JSON; compress legacy sidecars in place.
7. Guest transcript archiving (if decided).
8. Options → Storage shows how much disk the text uses, with a manual *Forget text older than…*.

**Risks.**
- A conversation redrawn from its transcript will not be pixel-identical to how it looked live (spinners, usage chips and wrap positions at the old width). The words and tool output are all there.
- Unbounded shell output (a runaway `yes`, a 2 GB build log) is stored in full. Compressed, a typical log is 10–20× smaller, but not free. (Decision below.)
- Full-screen programs (vim, htop, and TUIs on the alternate screen) never enter history, so they are not captured. That is true today too.
- Contentless FTS cannot return a snippet by itself; each hit costs one frame decompress (~1 ms for a 1 MB frame at zstd -3).
- Changing the format of the per-pane store touches `WindowState`, `Pane`, `ClosedStack` and `conv_index`, which other sessions often edit. The work lands in the order above, one step per commit.

**Build revision (2026-09-25, at claim).** What the code showed, and what changes from the design above:

- *Compression uses the stdlib.* The backend is stdlib-only Python 3.12 and has no zstd; Qt has zlib. Sealing writes a zlib stream (`.rtj.z`), and the 7-day pass rewrites it as xz (`.rtj.xz`, `lzma` preset 9e). Measured on 400 sidecars here, per file: zlib-6 5.3×, xz-9e 6.2×, zstd-19 6.1×. So xz matches zstd-19 with no new dependency on any platform.
- *The terminal holds 10,000 rows* (`LibVtermCore` ring `limit`, never changed by `src/`). A pane cannot show 50,000 restored rows, so restore puts back the newest rows the terminal can hold, and older text opens on demand: a first row `N earlier lines · open` runs `python3 -m relay_core.textjournal cat <id> | less -R` in a new pane (all lines, colours, `/` search). Search hits open the same way.
- *Capture point.* A row is journaled when it leaves the ring for good: overwritten at the limit, cleared (`CSI 3 J`, Clear scrollback), or cut by a smaller limit. Such rows are final (never popped back or rewrapped). The ring and screen stay in the per-pane tail file, raised from 5,000 lines / 512 KB to the ring's size, and are absorbed into the journal when the pane is pruned for good. Journal (older) + tail (newer) are disjoint and in order.
- *Journal id = the pane's `scrollback` id*, so a pane restored from its layout or from Recently closed keeps appending to the same journal. `$XDG_DATA_HOME/relay/text/<id>/` holds `meta.json` and `seg-NNNNNN.rtj` (open, plain JSONL) / `.rtj.z` / `.rtj.xz`.
- *Record format (JSONL, v1).* `{"s":n,"g":"1;38;5;196"}` defines style n as SGR parameters; `{"k":n,"u":uri}` defines link n (image, media and prose links only, as the saved form keeps); `{"t":text,"r":[[col,len,style,link],…],"m":marks}` is one logical line (soft-wrapped rows joined; columns in code points; `r`/`m` omitted when empty); `{"c":{"src","id","dir"}}` / `{"c":null}` open and close a conversation reference; `{"x":"clear"}` records a clear; `{"at":iso}` stamps time at most once a minute. Style and link tables are per segment, so each segment reads alone.
- *GhosttyCore* does not report evictions; with it the journal gets the tail at prune time only. libvterm is the core in use.

**Steps as built.**
1. `src/TextJournal.{h,cpp}` (QtCore library `relay-textjournal`): ANSI ↔ style-layer round trip, writer (append, seal on size/idle/close), reader of all three encodings. `backend/relay_core/textjournal.py`: reader, `cat` (ANSI out), `export` (plain), `recompress`. Tests both sides.
2. Engine: evicted rows collected in the core, drained by `TerminalSession`, surfaced through `TerminalBackend`. Pane appends them; per-pane tail caps raised; prune absorbs the tail; restore adds the `earlier lines` row.
3. Agent output rows get a row role (OSC 7772 `reply`), so the journal stores a conversation reference in their place; `cat` draws the turns from the transcript. `TranscriptReplay` draws tool output folded. Stop the per-session and rewound sidecar writers (readers stay for old files).
4. Index: `shell` source in `conv_index` (per command, contentless FTS5), Options *Search shell output* (off), Sessions check box, `has:shell`, open-a-hit.
5. Compression passes in the worker: seal idle open segments, xz after 7 days, compress legacy sidecars in place (readers take both).
6. Options → Storage (disk used by text, *Forget text older than…*) and a bundled `storage-cleanup` skill.

## Done means
- A plain shell pane that printed 50,000 lines, closed and reopened from Recently closed, shows all 50,000 with their colours and bold. A pane that ran a Relay conversation shows it redrawn from the transcript, with the shell lines before and after it in place.
- No line of text is stored in more than one of: transcript, journal, index. A script over `<data>/relay` finds no conversation text in any journal and no journal text in `index.db`.
- Session search for a word that only appeared in shell output finds nothing by default and finds it with *Search shell output* on or with `has:shell`.
- Journal segments that have been sealed for more than 7 days are zstd-19 on disk, and the stored bytes of the tree are at least 8× below the raw text.
- Failure looks like: a restored pane missing lines or colours, a turn printed twice on restore, or a shell hit in search while the flag is off.

## Decisions
- 2026-09-25, elliott: keep all shell output with no size cap. "yeah, show disk space in options and then a helper agent has a skill to help you clean out large records." Plan step 8 grows a bundled `storage-cleanup` skill: list journals and transcripts by size and age, show what a record holds, and delete one with confirmation.
- 2026-09-25, elliott: conversations are redrawn from the transcript on restore, "as long as its functionally equivalent". Tool output, folds, links and images must all come back; only live chrome such as spinners, usage chips and wrap width may differ.
- 2026-09-25, elliott (Q4): "yes, i am fine with those, thats a big improvement." Guests run in a Relay-owned `CLAUDE_CONFIG_DIR` / `CODEX_HOME`, split out as #5A37. That replaces design point 7 and plan step 7: there is no 21-day archive, because Relay's launches keep their transcripts under Relay.
