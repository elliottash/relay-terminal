"""Draw a journaled conversation reference from its transcript (card #HEY7).

`textjournal` can record a pane's conversation as a ``{"c": {"src", "id", "dir"}}`` reference
instead of the pane's rows; `render_reference` is the renderer `textjournal._conversation_renderer`
hands the `cat` command, so the reference prints as the conversation itself rather than as a bare
label.

The reading reuses the conversation index's own readers — `conv_index.session_entries` for a
relay session, `guest_sessions.parse_claude_transcript` / `parse_codex_rollout` for a guest — so
the items here are exactly the ones protocol 14.4's `conversation_get` answers with
(``{turn, kind, time, text, ...}``, one shape for all three sources). The drawing mirrors
src/TranscriptReplay.h (`relay::transcriptreplay::render`): prompts and commands as their own
lines, a reply as prose lines, one ``▸`` row per tool call simplified to a single line and cut at
100 chars, one blank line between turns; tool output and any other kind are not drawn, and empty
texts are skipped.

Nothing here raises on a transcript that is missing, unreadable or not what it claims to be: the
answer is `[]`, which `textjournal.render_lines` takes as "draw the conversation's label instead".
"""

from __future__ import annotations

from pathlib import Path

from relay_core import conv_index, guest_sessions
from relay_core.sessions import SessionStore

# Entries whose kind is not one of these are not messages (tool and command output, and anything a
# later index adds), so they are not drawn (TranscriptReplay.h `isDrawn`).
DRAWN_KINDS = ("prompt", "reply", "tool_call", "command")

# A tool-call row is one line however long its arguments were (TranscriptReplay.h `kCallWidth`).
CALL_WIDTH = 100


def _one_line(text: str, width: int = CALL_WIDTH) -> str:
    """`text` flattened to a single line (QString::simplified) and cut at `width` with an ellipsis."""
    flat = " ".join(text.split())
    return flat if len(flat) <= width else flat[: width - 1] + "…"


def render_items(items: list[dict]) -> list[str]:
    """The rows of one `conversation_get` items table, oldest first — `transcriptreplay::render`.

    A prompt or command is its own lines, a reply its prose lines, a tool call one ``▸`` row; one
    blank line between turns and none before the first; leading and trailing blanks trimmed.
    """
    rows: list[str] = []
    last_turn = -1
    for item in items:
        if not isinstance(item, dict):
            continue
        kind = item.get("kind")
        if kind not in DRAWN_KINDS:
            continue
        text = item.get("text")
        text = text if isinstance(text, str) else ""
        if not text.strip():
            continue
        try:
            turn = int(item.get("turn") or 0)
        except (TypeError, ValueError):
            turn = 0
        # One blank line between turns, the gap a live conversation leaves; none before the first.
        if last_turn >= 0 and turn != last_turn:
            rows.append("")
        last_turn = turn
        if kind == "tool_call":
            rows.append("▸ " + _one_line(text))
            continue
        rows.extend(text.split("\n"))
    # Trailing and leading blanks are the separator, not content.
    while rows and not rows[-1]:
        rows.pop()
    while rows and not rows[0]:
        rows.pop(0)
    return rows


def _guest_transcript(directory: Path, session_id: str) -> Path | None:
    """The transcript file of one guest session in its own directory, or None.

    claude names the file after the session (``<id>.jsonl``); a codex rollout's name ends with the
    session's UUID (``rollout-<timestamp>-<id>.jsonl``).
    """
    if not directory.is_dir():
        return None
    exact = directory / f"{session_id}.jsonl"
    if exact.is_file():
        return exact
    suffix = f"-{session_id}.jsonl"
    for path in sorted(directory.glob("*.jsonl")):
        if path.name.endswith(suffix):
            return path
    return None


def _reference_items(ref: dict) -> list[dict]:
    """The transcript items of the conversation `ref` points at, oldest first; [] when unreadable."""
    source = str(ref.get("src") or "relay")
    session_id = str(ref.get("id") or "")
    directory = str(ref.get("dir") or "")
    if not session_id:
        return []
    if source == "relay":
        root = Path(directory).expanduser() if directory else conv_index.sessions_root()
        try:
            data = SessionStore(root, index=False).load(session_id)
        except (OSError, ValueError):
            return []
        return conv_index.session_entries(data)
    if source in ("claude", "codex") and directory:
        path = _guest_transcript(Path(directory).expanduser(), session_id)
        if path is None:
            return []
        record = (guest_sessions.parse_claude_transcript(path) if source == "claude"
                  else guest_sessions.parse_codex_rollout(path))
        if not isinstance(record, dict):
            return []
        return [entry for entry in record.get("entries") or [] if isinstance(entry, dict)]
    return []


def render_reference(ref: dict, plain: bool = False) -> list[str]:
    """The conversation `ref` (``{"src", "id", "dir"}``) points at, drawn as terminal lines.

    `plain` is accepted for the caller's signature; the rows are always plain text with no ANSI —
    the C++ side inks them itself, exactly as `TranscriptReplay.h` leaves colours to its caller.

    Never raises: a reference that cannot be read is `[]`, and `textjournal.render_lines` prints
    the conversation's label in its place.
    """
    del plain   # rows are always plain; the argument only keeps the renderer's signature
    try:
        if not isinstance(ref, dict):
            return []
        return render_items(_reference_items(ref))
    except Exception:   # noqa: BLE001 - a transcript is never the journal's problem
        return []
