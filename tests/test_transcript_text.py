"""transcript_text (card #HEY7): a journaled conversation reference drawn from its transcript.

`render_items` is the pure mirror of src/TranscriptReplay.h's `render`; `render_reference` finds
the transcript of a `{"src", "id", "dir"}` reference through the conversation index's own readers
and must never raise — a missing or corrupt transcript is `[]`, which `textjournal.render_lines`
takes as "print the conversation's label instead".
"""

import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "backend"))

from relay_core import conv_index  # noqa: E402
from relay_core import transcript_text as tt  # noqa: E402

CLAUDE_ID = "11111111-2222-3333-4444-555555555555"
CODEX_ID = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"


# ----- fixtures ---------------------------------------------------------------------------------

def write_relay_session(directory: Path, session_id: str) -> Path:
    """One saved relay session: two turns, a tool call and its output, as SessionStore wrote it."""
    data = {
        "id": session_id,
        "updated": 1700000000,
        "checkpoints": {"items": [
            {"turn": 1, "prompt": "first prompt", "time": 1700000001, "locations": {"0": 1}},
            {"turn": 2, "prompt": "second prompt", "time": 1700000002, "locations": {"0": 4}},
        ]},
        "messages": [
            {"role": "user", "content": "first prompt"},
            {"role": "assistant", "content": "reply one",
             "tool_calls": [{"function": {"name": "Read", "arguments": {"path": "a.py"}}}]},
            {"role": "tool", "content": "the file contents, not drawn"},
            {"role": "user", "content": "second prompt"},
            {"role": "assistant", "content": "reply two"},
        ],
    }
    path = directory / f"{session_id}.json"
    path.write_text(json.dumps(data), encoding="utf-8")
    return path


def write_claude_transcript(directory: Path) -> Path:
    lines = [
        {"type": "user", "sessionId": CLAUDE_ID, "cwd": "/tmp/project",
         "timestamp": "2025-01-01T10:00:00Z",
         "message": {"content": [{"type": "text", "text": "hello claude"}]}},
        {"type": "assistant", "sessionId": CLAUDE_ID, "timestamp": "2025-01-01T10:00:05Z",
         "message": {"content": [{"type": "text", "text": "hi there"},
                                 {"type": "tool_use", "name": "Bash",
                                  "input": {"command": "ls -la"}}]}},
        # A tool-result carrier: user-typed it is not, so it is not a prompt (and not drawn).
        {"type": "user", "sessionId": CLAUDE_ID, "timestamp": "2025-01-01T10:00:06Z",
         "message": {"content": [{"type": "tool_result", "content": "file1\nfile2"}]}},
        {"type": "user", "sessionId": CLAUDE_ID, "timestamp": "2025-01-01T10:01:00Z",
         "message": {"content": [{"type": "text", "text": "second ask"}]}},
        {"type": "assistant", "sessionId": CLAUDE_ID, "timestamp": "2025-01-01T10:01:05Z",
         "message": {"content": [{"type": "text", "text": "second reply"}]}},
    ]
    path = directory / f"{CLAUDE_ID}.jsonl"
    path.write_text("".join(json.dumps(line) + "\n" for line in lines), encoding="utf-8")
    return path


def write_codex_rollout(directory: Path) -> Path:
    lines = [
        {"type": "session_meta", "timestamp": "2025-01-01T10:00:00Z",
         "payload": {"id": CODEX_ID, "cwd": "/tmp/project", "originator": "relay"}},
        {"type": "response_item", "timestamp": "2025-01-01T10:00:01Z",
         "payload": {"type": "message", "role": "user",
                     "content": [{"type": "input_text", "text": "codex prompt"}]}},
        {"type": "response_item", "timestamp": "2025-01-01T10:00:05Z",
         "payload": {"type": "message", "role": "assistant",
                     "content": [{"type": "output_text", "text": "codex reply"}]}},
        {"type": "response_item", "timestamp": "2025-01-01T10:00:06Z",
         "payload": {"type": "function_call", "name": "shell",
                     "arguments": "{\"command\": \"pwd\"}"}},
        # Command output is a response_item too; neither parser nor renderer draws it.
        {"type": "response_item", "timestamp": "2025-01-01T10:00:07Z",
         "payload": {"type": "function_call_output", "output": "/tmp/project"}},
    ]
    path = directory / f"rollout-2025-01-01T10-00-00-{CODEX_ID}.jsonl"
    path.write_text("".join(json.dumps(line) + "\n" for line in lines), encoding="utf-8")
    return path


# ----- render_items: the TranscriptReplay.h mirror -----------------------------------------------

def test_items_draw_prompt_reply_and_one_call_row_per_tool_call():
    rows = tt.render_items([
        {"turn": 1, "kind": "prompt", "text": "the prompt"},
        {"turn": 1, "kind": "reply", "text": "the reply"},
        {"turn": 1, "kind": "tool_call", "text": "Read a.py"},
        {"turn": 1, "kind": "command", "text": "ls -la"},
    ])
    assert rows == ["the prompt", "the reply", "▸ Read a.py", "ls -la"]


def test_items_leave_one_blank_between_turns_and_none_before_the_first():
    rows = tt.render_items([
        {"turn": 1, "kind": "prompt", "text": "one"},
        {"turn": 1, "kind": "reply", "text": "two"},
        {"turn": 2, "kind": "prompt", "text": "three"},
        {"turn": 3, "kind": "reply", "text": "four"},
    ])
    assert rows == ["one", "two", "", "three", "", "four"]


def test_items_do_not_draw_tool_output_or_other_kinds():
    rows = tt.render_items([
        {"turn": 1, "kind": "prompt", "text": "ask"},
        {"turn": 1, "kind": "tool_output", "text": "pages of output"},
        {"turn": 1, "kind": "command_output", "text": "more output"},
        {"turn": 1, "kind": "reply", "text": "answer"},
    ])
    assert rows == ["ask", "answer"]


def test_items_skip_empty_texts_and_trim_edge_blanks():
    rows = tt.render_items([
        {"turn": 1, "kind": "prompt", "text": "ask"},
        {"turn": 1, "kind": "reply", "text": "   \n  "},
        {"turn": 2, "kind": "reply", "text": ""},
        {"turn": 2, "kind": "reply", "text": "answer\n"},
    ])
    # The empty replies leave no rows; the turn-change blank survives between real turns and the
    # trailing blank of "answer\n" is trimmed.
    assert rows == ["ask", "", "answer"]


def test_items_tool_call_is_simplified_and_cut_at_100_chars():
    long_call = "Read   " + "x" * 200 + "\nsecond line"
    rows = tt.render_items([{"turn": 1, "kind": "tool_call", "text": long_call}])
    assert rows == ["▸ " + ("Read " + "x" * 200 + " second line")[:99] + "…"]
    assert len(rows[0]) == 2 + 100


def test_items_keep_prompt_and_reply_lines_as_they_are():
    rows = tt.render_items([
        {"turn": 1, "kind": "prompt", "text": "line one\nline two"},
        {"turn": 1, "kind": "reply", "text": "prose\n\nwith a gap"},
    ])
    assert rows == ["line one", "line two", "prose", "", "with a gap"]


# ----- render_reference: one end-to-end read per source ------------------------------------------

def test_relay_reference(tmp_path):
    session_id = "a" * 32
    store = tmp_path / "sessions"
    store.mkdir()
    write_relay_session(store, session_id)
    rows = tt.render_reference({"src": "relay", "id": session_id, "dir": str(store)})
    assert rows == ["first prompt", "reply one", "▸ Read {\"path\": \"a.py\"}",
                    "", "second prompt", "reply two"]
    assert "the file contents, not drawn" not in "\n".join(rows)


def test_claude_reference(tmp_path):
    directory = tmp_path / "-tmp-project"
    directory.mkdir()
    write_claude_transcript(directory)
    rows = tt.render_reference({"src": "claude", "id": CLAUDE_ID, "dir": str(directory)})
    assert rows == ["hello claude", "hi there", "▸ Bash {\"command\": \"ls -la\"}",
                    "", "second ask", "second reply"]


def test_codex_reference(tmp_path):
    directory = tmp_path / "2025" / "01" / "01"
    directory.mkdir(parents=True)
    write_codex_rollout(directory)
    rows = tt.render_reference({"src": "codex", "id": CODEX_ID, "dir": str(directory)})
    assert rows == ["codex prompt", "codex reply", "▸ shell {\"command\": \"pwd\"}"]


def test_plain_argument_is_accepted_and_rows_carry_no_ansi(tmp_path):
    session_id = "b" * 32
    store = tmp_path / "sessions"
    store.mkdir()
    write_relay_session(store, session_id)
    ref = {"src": "relay", "id": session_id, "dir": str(store)}
    assert tt.render_reference(ref, plain=True) == tt.render_reference(ref, plain=False)
    assert not any("\x1b" in row for row in tt.render_reference(ref, plain=True))


# ----- failure is [], never an exception ----------------------------------------------------------

def test_missing_relay_id_is_empty(tmp_path):
    assert tt.render_reference({"src": "relay", "id": "c" * 32, "dir": str(tmp_path)}) == []


def test_missing_guest_id_is_empty(tmp_path):
    write_claude_transcript(tmp_path)
    assert tt.render_reference({"src": "claude", "id": CODEX_ID, "dir": str(tmp_path)}) == []
    assert tt.render_reference({"src": "codex", "id": CODEX_ID, "dir": str(tmp_path)}) == []


def test_corrupt_transcripts_are_empty(tmp_path):
    store = tmp_path / "sessions"
    store.mkdir()
    (store / f"{'d' * 32}.json").write_text("{not json", encoding="utf-8")
    assert tt.render_reference({"src": "relay", "id": "d" * 32, "dir": str(store)}) == []
    (tmp_path / f"{CLAUDE_ID}.jsonl").write_text("garbage\n{broken\n", encoding="utf-8")
    assert tt.render_reference({"src": "claude", "id": CLAUDE_ID, "dir": str(tmp_path)}) == []


def test_bad_references_are_empty():
    assert tt.render_reference(None) == []
    assert tt.render_reference({}) == []
    assert tt.render_reference({"src": "claude", "id": CLAUDE_ID}) == []   # no dir
    assert tt.render_reference({"src": "unknown", "id": "x", "dir": "/tmp"}) == []


def test_reference_with_dir_never_asks_the_default_sessions_root(tmp_path, monkeypatch):
    def fail():
        raise AssertionError("the real home directory must not be consulted")

    monkeypatch.setattr(conv_index, "sessions_root", fail)
    session_id = "e" * 32
    write_relay_session(tmp_path, session_id)
    assert tt.render_reference({"src": "relay", "id": session_id, "dir": str(tmp_path)}) != []
