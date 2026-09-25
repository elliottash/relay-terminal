"""The pane text journal's Python side (card #HEY7): it reads what the GUI's writer wrote, prints
it as the serializer would, cuts it into commands for the index, and seals and recompresses.

`tests/fixtures/textjournal/fixture-0001` was written by src/TextJournal.cpp's Writer (one sealed
zlib segment, one open segment), so the first tests hold the two languages to one format.
"""

import json
import lzma
import os
import shutil
import sys
import time
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "backend"))

from relay_core import textjournal as tj  # noqa: E402

FIXTURE = ROOT / "tests" / "fixtures" / "textjournal" / "fixture-0001"


def test_reads_the_cxx_writers_segments():
    kinds = [event.kind for event in tj.events(FIXTURE)]
    assert kinds == ["line", "line", "line", "conversation", "conversation_end", "clear", "line"]
    lines = tj.lines(FIXTURE)
    assert lines[0].text == "user@host:~$ ls"
    assert lines[0].marks == tj.MARK_PROMPT_START | tj.MARK_USER_SHELL
    # The soft-wrapped pair came in as two rows and is one line here.
    assert lines[2].text == "a long line that wrapped italic"
    assert tj.lines(FIXTURE, since_clear=True)[0].text == "😀 emoji done"


def test_prints_what_the_serializer_wrote():
    out = tj.render_lines(FIXTURE, conversation=None)
    assert out[0] == "\x1b[1;38;5;2muser@host\x1b[0m:~$ ls"
    assert out[1] == "a.txt  \x1b[38;2;255;128;0mb.txt\x1b[0m"
    assert out[2] == "a long line that wrapped \x1b[3mitalic\x1b[0m"
    # Columns count code points: the emoji is one.
    assert out[-1] == "😀 \x1b[4memoji\x1b[0m done"
    assert "relay conversation 0123456789abcdef0123456789abcdef" in out[3]


def test_plain_and_since_clear():
    plain = tj.render_lines(FIXTURE, plain=True)
    assert all("\x1b" not in line for line in plain)
    assert tj.render_lines(FIXTURE, plain=True, since_clear=True) == ["😀 emoji done"]


def test_links_round_trip():
    line = tj.Line("xx yy", [tj.Span(0, 2, "", "relay-image:/tmp/a.png"), tj.Span(3, 2, "1", "")])
    assert tj.to_ansi(line) == "\x1b]8;;relay-image:/tmp/a.png\x1b\\xx\x1b]8;;\x1b\\ \x1b[1myy\x1b[0m"


def _journal(tmp_path, records_by_segment: dict[str, list[dict]]) -> Path:
    directory = tmp_path / "text" / "abcdef12-0000"
    directory.mkdir(parents=True)
    (directory / "meta.json").write_text(json.dumps({"v": 1, "lines": 0}))
    for name, records in records_by_segment.items():
        raw = "".join(json.dumps(r) + "\n" for r in records).encode()
        path = directory / name
        if name.endswith(".z"):
            raw = zlib.compress(raw)
        elif name.endswith(".xz"):
            raw = lzma.compress(raw)
        path.write_bytes(raw)
    return directory


def test_most_compressed_copy_wins_and_torn_lines_are_skipped(tmp_path):
    directory = _journal(tmp_path, {
        "seg-000001.rtj.xz": [{"t": "from xz"}],
        "seg-000001.rtj": [{"t": "stale raw copy"}],
        "seg-000002.rtj": [{"t": "open"}],
    })
    with open(directory / "seg-000002.rtj", "a") as handle:
        handle.write('{"t": "torn')
    assert [line.text for line in tj.lines(directory)] == ["from xz", "open"]


def test_commands_cut_at_prompts_and_conversations(tmp_path):
    directory = _journal(tmp_path, {"seg-000001.rtj": [
        {"t": "before any prompt"},
        {"t": "$ make", "m": tj.MARK_PROMPT_START},
        {"t": "cc -o x x.c"},
        {"t": "error: boom"},
        {"c": {"src": "relay", "id": "0" * 32}},
        {"c": None},
        {"t": "$ ls", "m": tj.MARK_USER_SHELL},
        {"t": "x"},
    ]})
    commands = tj.commands(directory)
    assert [(c.prompt, c.output) for c in commands] == [
        ("", ["before any prompt"]),
        ("$ make", ["cc -o x x.c", "error: boom"]),
        ("$ ls", ["x"]),
    ]
    assert [c.index for c in commands] == [0, 1, 2]


def test_seal_idle_and_recompress(tmp_path):
    root = tmp_path / "text"
    directory = _journal(tmp_path, {"seg-000001.rtj": [{"t": "a"}], "seg-000002.rtj": [{"t": "b"}]})
    old = time.time() - 2 * tj.SEAL_IDLE_SECONDS
    os.utime(directory / "seg-000001.rtj", (old, old))
    assert tj.seal_idle(root) == 1
    assert (directory / "seg-000001.rtj.z").exists()
    assert not (directory / "seg-000001.rtj").exists()
    assert (directory / "seg-000002.rtj").exists()   # fresh: a live writer may hold it

    # Not old enough yet, then old enough.
    assert tj.recompress(root) == 0
    week_ago = time.time() - tj.RECOMPRESS_AFTER_SECONDS - 60
    os.utime(directory / "seg-000001.rtj.z", (week_ago, week_ago))
    assert tj.recompress(root) == 1
    assert (directory / "seg-000001.rtj.xz").exists()
    assert not (directory / "seg-000001.rtj.z").exists()
    assert [line.text for line in tj.lines(directory)] == ["a", "b"]


def test_usage_and_forget(tmp_path):
    root = tmp_path / "text"
    directory = _journal(tmp_path, {"seg-000001.rtj": [{"t": "a"}]})
    (directory / "meta.json").write_text(json.dumps({"v": 1, "lines": 1, "cwd": "/w"}))
    report = tj.usage(root)
    assert report["lines"] == 1 and report["bytes"] > 0
    assert report["journals"][0]["cwd"] == "/w"
    long_ago = time.time() - 40 * 86400
    for entry in directory.iterdir():
        os.utime(entry, (long_ago, long_ago))
    assert tj.forget_older_than(30 * 86400, root, keep={directory.name}) == 0
    assert tj.forget_older_than(30 * 86400, root) == 1
    assert not directory.exists()


def test_cat_command_line(tmp_path, capsys):
    target = tmp_path / "copy"
    shutil.copytree(FIXTURE, target)
    assert tj.main(["cat", str(target), "--plain", "--no-conversations"]) == 0
    out = capsys.readouterr().out.splitlines()
    assert out[0] == "user@host:~$ ls"
    assert out[-1] == "😀 emoji done"
    assert tj.main(["cat", str(tmp_path / "missing")]) == 1


def test_refuses_ids_that_are_not_ids(tmp_path):
    # An id resolves under the root; anything else must be an existing directory named outright.
    assert tj.journal_dir("0f3c2a1b-aaaa", root=tmp_path) == tmp_path / "0f3c2a1b-aaaa"
    assert tj.journal_dir(str(tmp_path / "no-such-dir"), root=tmp_path) is None
    assert tj.is_journal_id("0f3c2a1b-aaaa-bbbb-cccc-123456789abc")
    assert not tj.is_journal_id("../x")


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-q"]))
