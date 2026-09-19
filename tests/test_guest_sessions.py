"""Guest sessions: the `claude` and `codex` conversation sources (protocol 26.7).

The fixtures are the real guests' line formats (a claude `projects/<slug>/<id>.jsonl`, a codex
`rollout-*.jsonl` and a subset of the codex `state_*.sqlite`), built into temporary homes here
and re-read from `tests/fixtures/guest/` by `FixtureFormat` so a change in either guest's
transcript shape fails a test instead of quietly emptying the sessions pane. Nothing in this
file touches `~/.claude`, `~/.codex` or the real index.
"""
import json
import os
import shutil
import sqlite3
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from relay_core import conv_index, guest, guest_sessions
from relay_core.conv_index import GUEST_SOURCES, ConversationIndex

CLAUDE_ID = "ea11ece1-7ec2-4597-8639-32fb1f43f073"
CODEX_ID = "01a0b785-bca3-7b22-aa25-4c4c444a276d"
CODEX_NAME = f"rollout-2026-09-18T22-36-30-{CODEX_ID}.jsonl"
CWD = "/home/elliott/repos/relay-terminal"
FIXTURES = Path(__file__).parent / "fixtures" / "guest"


# ----- the guests' line formats ----------------------------------------------------------------
# Every shape below is copied from this machine's own transcripts; the fixture test at the bottom
# reads them back from tests/fixtures/guest/ so both stay honest.

def claude_header(session_id=CLAUDE_ID) -> list[dict]:
    """The bookkeeping lines claude opens a transcript with."""
    return [
        {"type": "last-prompt", "leafUuid": "l1", "sessionId": session_id},
        {"type": "mode", "mode": "normal", "sessionId": session_id},
        {"type": "permission-mode", "permissionMode": "bypassPermissions", "sessionId": session_id},
        {"type": "file-history-snapshot", "messageId": "m0",
         "snapshot": {"messageId": "m0", "trackedFileBackups": {}, "timestamp": "2026-09-17T01:14:06.474Z"},
         "isSnapshotUpdate": False},
    ]


def claude_prompt_line(prompt, *, index=0, session_id=CLAUDE_ID, cwd=CWD, sidechain=False) -> dict:
    return {"parentUuid": None, "isSidechain": sidechain, "promptId": f"p{index}", "type": "user",
            "message": {"role": "user", "content": prompt}, "uuid": f"u{index}",
            "timestamp": "2026-09-17T01:13:47.871Z", "userType": "external", "entrypoint": "cli",
            "cwd": cwd, "sessionId": session_id, "version": "2.1.274", "gitBranch": "HEAD"}


def claude_tool_result_line(*, index=0, session_id=CLAUDE_ID, cwd=CWD) -> dict:
    """The tool result claude sends back as a `user` line: not a prompt the user typed."""
    return {"parentUuid": f"u{index}", "isSidechain": False, "type": "user",
            "message": {"role": "user", "content": [
                {"tool_use_id": "t1", "type": "tool_result", "content": "ok"}]},
            "uuid": f"t{index}", "timestamp": "2026-09-17T01:13:48.100Z",
            "cwd": cwd, "sessionId": session_id}


def claude_reply_line(reply, *, index=0, session_id=CLAUDE_ID, cwd=CWD, sidechain=False,
                      tool_use=True) -> dict:
    content: list[dict] = [{"type": "text", "text": reply}]
    if tool_use:
        content.append({"type": "tool_use", "id": "t1", "name": "Edit",
                        "input": {"file_path": "/home/elliott/repos/relay-terminal/src/Pane.h",
                                  "old_string": "a", "new_string": "b"}})
    return {"parentUuid": "u0", "isSidechain": sidechain, "type": "assistant",
            "message": {"model": "claude-opus-4-1", "id": f"msg{index}", "type": "message",
                        "role": "assistant", "content": content,
                        "usage": {"input_tokens": 4, "output_tokens": 9}},
            "uuid": f"a{index}", "timestamp": "2026-09-17T01:14:01.000Z",
            "cwd": cwd, "sessionId": session_id, "gitBranch": "HEAD"}


def claude_lines(*, session_id=CLAUDE_ID, cwd=CWD, prompts=("fix the pane drag",),
                 replies=("Fixed it in Pane.h.",), custom_title="pane-drag-fix", ai_title="Pane drag",
                 sidechain=False, tool_result_only=True, tool_use=True) -> list[dict]:
    """The lines claude writes for one session, in the order it writes them: bookkeeping, then
    each turn's prompt (with the tool-result line claude echoes back), reply and tool call, then
    the titles it re-emits as lines of their own at the end."""
    lines = claude_header(session_id)
    for index, prompt in enumerate(prompts):
        lines.append(claude_prompt_line(prompt, index=index, session_id=session_id, cwd=cwd,
                                        sidechain=sidechain))
        if tool_result_only:
            lines.append(claude_tool_result_line(index=index, session_id=session_id, cwd=cwd))
        if index < len(replies):
            lines.append(claude_reply_line(replies[index], index=index, session_id=session_id, cwd=cwd,
                                           sidechain=sidechain, tool_use=tool_use))
    if custom_title is not None:
        lines.append({"type": "custom-title", "customTitle": custom_title, "sessionId": session_id})
    if ai_title is not None:
        lines.append({"type": "ai-title", "aiTitle": ai_title, "sessionId": session_id})
    return lines


def codex_prompt_line(prompt, *, session_id=CODEX_ID, cwd=CWD) -> dict:
    return {"timestamp": "2026-09-19T02:36:33.000Z", "ordinal": 4, "type": "response_item",
            "payload": {"type": "message", "id": "msg1", "role": "user",
                        "content": [{"type": "input_text", "text": prompt}]}}


def codex_reply_line(reply, *, session_id=CODEX_ID) -> dict:
    return {"timestamp": "2026-09-19T02:36:38.000Z", "ordinal": 7, "type": "response_item",
            "payload": {"type": "message", "id": "msg2", "role": "assistant",
                        "content": [{"type": "output_text", "text": reply}]}}


def codex_lines(*, session_id=CODEX_ID, cwd=CWD, prompt="what does the diff view do",
                reply="It opens a Relay diff.", developer=True) -> list[dict]:
    """The lines codex writes for one session (rollout), in order."""
    lines: list[dict] = [
        {"timestamp": "2026-09-19T02:36:31.797Z", "ordinal": 0, "type": "session_meta",
         "payload": {"session_id": session_id, "id": session_id, "timestamp": "2026-09-19T02:36:30.258Z",
                     "cwd": cwd, "runtime_workspace_roots": [cwd], "cli_version": "0.36.0"}},
        {"timestamp": "2026-09-19T02:36:31.798Z", "ordinal": 1, "type": "event_msg",
         "payload": {"type": "task_started", "turn_id": "turn-1", "started_at": 1789785391}},
    ]
    if developer:
        lines.append({"timestamp": "2026-09-19T02:36:32.080Z", "ordinal": 2, "type": "response_item",
                      "payload": {"type": "message", "id": "msg0", "role": "developer",
                                  "content": [{"type": "input_text", "text": "<skills_instructions>"}]}})
    lines += [
        {"timestamp": "2026-09-19T02:36:32.087Z", "ordinal": 3, "type": "turn_context",
         "payload": {"turn_id": "turn-1", "cwd": cwd, "workspace_roots": [cwd]}},
        codex_prompt_line(prompt, session_id=session_id, cwd=cwd),
        {"timestamp": "2026-09-19T02:36:35.138Z", "ordinal": 5, "type": "token_usage_record",
         "payload": {"thread_id": session_id, "turn_id": "turn-1", "total_tokens": 42}},
        {"timestamp": "2026-09-19T02:36:36.000Z", "ordinal": 6, "type": "response_item",
         "payload": {"type": "function_call", "name": "shell", "arguments": '{"command": "ls -la"}'}},
        codex_reply_line(reply, session_id=session_id),
    ]
    return lines


def write_claude(home: Path, lines, *, cwd=CWD, session_id=CLAUDE_ID) -> Path:
    folder = home / ".claude" / "projects" / guest_sessions.claude_slug(cwd)
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / f"{session_id}.jsonl"
    path.write_text("".join(json.dumps(line) + "\n" for line in lines), encoding="utf-8")
    return path


def write_codex(home: Path, lines, *, day="2026/09/18", name=CODEX_NAME) -> Path:
    folder = home / ".codex" / "sessions" / day
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / name
    path.write_text("".join(json.dumps(line) + "\n" for line in lines), encoding="utf-8")
    return path


def write_state_db(home: Path, rows, *, version=5, extra_columns=()) -> Path:
    """A codex threads database holding `rows` of `{id, rollout_path, cwd, title, ...}`."""
    path = home / ".codex" / f"state_{version}.sqlite"
    path.parent.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(str(path))
    columns = ["id TEXT PRIMARY KEY", "rollout_path TEXT NOT NULL", "created_at INTEGER NOT NULL",
               "updated_at INTEGER NOT NULL", "source TEXT NOT NULL", "cwd TEXT NOT NULL",
               "title TEXT NOT NULL", "first_user_message TEXT NOT NULL", "name TEXT"]
    columns += [f"{name} TEXT" for name in extra_columns]
    db.execute("CREATE TABLE threads (%s)" % ", ".join(columns))
    for row in rows:
        keys = [column.split()[0] for column in columns]
        db.execute("INSERT INTO threads(%s) VALUES(%s)" % (", ".join(keys), ", ".join("?" * len(keys))),
                   [row.get(key, "" if "TEXT" in columns[keys.index(key)] else 0) for key in keys])
    db.commit()
    db.close()
    return path


def index_in(root: Path) -> ConversationIndex:
    return ConversationIndex(path=root / "index.db")


# ----- records and resume commands --------------------------------------------------------------

class ResumeCommand(unittest.TestCase):
    def test_claude_resumes_and_forks(self):
        self.assertEqual(["claude", "-r", CLAUDE_ID],
                         guest_sessions.resume_command("claude", CLAUDE_ID))
        self.assertEqual(["claude", "-r", CLAUDE_ID, "--fork-session"],
                         guest_sessions.resume_command("claude", CLAUDE_ID, fork=True))

    def test_codex_resume_and_fork_are_subcommands(self):
        self.assertEqual(["codex", "resume", CODEX_ID],
                         guest_sessions.resume_command("codex", CODEX_ID))
        self.assertEqual(["codex", "fork", CODEX_ID],
                         guest_sessions.resume_command("codex", CODEX_ID, fork=True))

    def test_bad_guest_or_missing_id_is_an_error(self):
        for source in ("gemini", "", "Claude"):
            with self.subTest(source=source), self.assertRaises(ValueError):
                guest_sessions.resume_command(source, CLAUDE_ID)
        for session_id in ("", None):
            with self.subTest(session_id=session_id), self.assertRaises(ValueError):
                guest_sessions.resume_command("claude", session_id)

    def test_resume_spawn_carries_the_working_directory(self):
        """claude looks a session up under the directory it is started in, so the argv alone
        cannot resume one: the pane must chdir to the session's workspace first."""
        self.assertEqual({"argv": ["claude", "-r", CLAUDE_ID], "cwd": CWD},
                         guest_sessions.resume_spawn("claude", CLAUDE_ID, CWD))
        self.assertEqual({"argv": ["codex", "fork", CODEX_ID], "cwd": CWD},
                         guest_sessions.resume_spawn("codex", CODEX_ID, CWD, fork=True))
        # No workspace on the record is "" — the spawner keeps the pane's own directory.
        self.assertEqual("", guest_sessions.resume_spawn("claude", CLAUDE_ID, None)["cwd"])
        with self.assertRaises(ValueError):
            guest_sessions.resume_spawn("gemini", CLAUDE_ID, CWD)


class RecordFields(unittest.TestCase):
    def test_to_record_is_exactly_the_protocol_shape(self):
        record = guest_sessions.to_record(
            {"source": "claude", "id": CLAUDE_ID, "title": "  pane   drag \n fix ", "mtime": 1234.5,
             "workspace": CWD, "message_count": 4, "entries": [{"text": "x"}]})
        self.assertEqual({"source", "id", "title", "mtime", "workspace", "message_count",
                          "resume_command", "resume_cwd"}, set(record))
        self.assertEqual(CWD, record["resume_cwd"], "the argv is spawned in the session's own cwd")
        self.assertEqual(("claude", CLAUDE_ID, "pane drag fix", 1234.5, CWD, 4,
                          ["claude", "-r", CLAUDE_ID]),
                         (record["source"], record["id"], record["title"], record["mtime"],
                          record["workspace"], record["message_count"], record["resume_command"]))

    def test_item_to_record_maps_an_index_row(self):
        record = guest_sessions.item_to_record(
            {"source": "codex", "session_id": CODEX_ID, "title": "Diff view", "updated": 99.0,
             "workspace": CWD, "turns": 6}, fork=True)
        self.assertEqual(("codex", CODEX_ID, "Diff view", 99.0, CWD, 6, ["codex", "fork", CODEX_ID]),
                         (record["source"], record["id"], record["title"], record["mtime"],
                          record["workspace"], record["message_count"], record["resume_command"]))

    def test_annotate_items_touches_only_the_guest_rows(self):
        agent = {"source": "agent", "session_id": "abc", "turns": 3, "updated": 5.0}
        claude = {"source": "claude", "session_id": CLAUDE_ID, "turns": 4, "updated": 6.0,
                  "workspace": CWD, "title": "pane drag"}
        codex = {"source": "codex", "session_id": CODEX_ID, "turns": 2, "updated": 7.0,
                 "workspace": CWD, "title": "diff view"}
        out = guest_sessions.annotate_items([agent, claude, codex])
        self.assertEqual(agent, out[0], "a non-guest item is returned as the index gave it")
        self.assertEqual(["claude", "-r", CLAUDE_ID], out[1]["resume_command"])
        # The spawn payload is complete: the directory the command has to run in comes with it.
        self.assertEqual([CWD, CWD], [out[1]["resume_cwd"], out[2]["resume_cwd"]])
        self.assertEqual([CWD, CWD], [out[1]["workspace"], out[2]["workspace"]])
        self.assertNotIn("resume_cwd", out[0], "a non-guest row gains nothing")
        self.assertEqual((CLAUDE_ID, 6.0, 4), (out[1]["id"], out[1]["mtime"], out[1]["message_count"]))
        self.assertEqual("pane drag", out[1]["title"], "the index's own fields stay put")
        self.assertEqual(["codex", "resume", CODEX_ID], out[2]["resume_command"])
        fork = guest_sessions.annotate_items([claude, codex], fork=True)
        self.assertEqual([["claude", "-r", CLAUDE_ID, "--fork-session"], ["codex", "fork", CODEX_ID]],
                         [item["resume_command"] for item in fork])


# ----- claude transcripts ------------------------------------------------------------------------

class ClaudeParsing(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.home = self.root / "home"
        self.home.mkdir()

    def parse(self, lines, **kwargs):
        path = write_claude(self.home, lines, **kwargs)
        return guest_sessions.parse_claude_transcript(path), path

    def test_record_fields_and_counts(self):
        parsed, path = self.parse(claude_lines(prompts=("first ask", "second ask"),
                                              replies=("one answer", "two answer")))
        self.assertEqual(("claude", CLAUDE_ID, CWD, 4), (parsed["source"], parsed["id"],
                                                         parsed["workspace"], parsed["message_count"]))
        self.assertAlmostEqual(path.stat().st_mtime, parsed["mtime"], places=6)
        # created comes from the transcript's own first timestamp, not the file's mtime.
        self.assertLess(parsed["created"], parsed["mtime"] - 86400)
        self.assertEqual("pane-drag-fix", parsed["title"])
        self.assertEqual("custom", parsed["title_kind"])

    def test_title_precedence(self):
        cases = ((("pane-drag-fix", "Pane drag"), "pane-drag-fix", "custom"),
                 ((None, "Pane drag"), "Pane drag", "ai"),
                 ((None, None), "fix the pane drag", "prompt"))
        for (custom, ai), title, kind in cases:
            with self.subTest(custom=custom, ai=ai):
                parsed, _ = self.parse(claude_lines(custom_title=custom, ai_title=ai))
                self.assertEqual((title, kind), (parsed["title"], parsed["title_kind"]))

    def test_a_compaction_summary_is_the_last_resort_title(self):
        lines = claude_lines(custom_title=None, ai_title=None)
        lines.insert(4, {"type": "summary", "summary": "Compact summary of the pane work",
                         "leafUuid": "u0"})
        parsed, _ = self.parse(lines)
        self.assertEqual(("Compact summary of the pane work", "summary"),
                         (parsed["title"], parsed["title_kind"]))

    def test_sidechains_and_tool_results_are_not_messages(self):
        parsed, _ = self.parse(claude_lines(sidechain=True))
        self.assertEqual(0, parsed["message_count"])
        self.assertEqual([], parsed["entries"])
        parsed, _ = self.parse(claude_lines(sidechain=False, tool_result_only=True, tool_use=False,
                                            custom_title=None, ai_title=None))
        self.assertEqual(2, parsed["message_count"])       # one prompt, one reply
        self.assertEqual(["prompt", "reply"], [row["kind"] for row in parsed["entries"]])

    def test_entries_carry_kinds_turns_and_capped_tool_arguments(self):
        parsed, _ = self.parse(claude_lines(prompts=("first ask", "second ask"), replies=("answer",)))
        self.assertEqual(["prompt", "reply", "tool_call", "prompt"], [row["kind"] for row in parsed["entries"]])
        self.assertEqual([1, 1, 1, 2], [row["turn"] for row in parsed["entries"]])
        call = parsed["entries"][2]["text"]
        self.assertTrue(call.startswith("Edit "))
        self.assertIn("Pane.h", call)
        self.assertLessEqual(len(call), 5 + guest_sessions.MAX_TOOL_ARGUMENTS)

    def test_empty_transcript_falls_back_to_the_file_name_and_slug(self):
        # A slug with no dash inside a real name decodes exactly; a dash does not (see Slugs).
        parsed, path = self.parse([], session_id="1f0d0b3e-0000-4000-8000-000000000001", cwd="/tmp/one")
        self.assertEqual("1f0d0b3e-0000-4000-8000-000000000001", parsed["id"])
        self.assertEqual("/tmp/one", parsed["workspace"])
        self.assertEqual(0, parsed["message_count"])

    def test_unreadable_file_is_none(self):
        self.assertIsNone(guest_sessions.parse_claude_transcript(self.root / "missing.jsonl"))

    def test_the_file_name_is_the_session_id_even_when_a_line_disagrees(self):
        """claude resolves `-r <id>` to `<slug>/<id>.jsonl`, so the name is what resumes — and
        it is the key `_walk` looks a file up by before reading it, so the record must carry the
        same one or the index row it wrote is never found again (see the reconcile test)."""
        named = "4" * 8 + "-0000-4000-8000-000000000004"
        parsed, _ = self.parse(claude_lines(session_id="inside-says-something-else"),
                               session_id=named)
        self.assertEqual(named, parsed["id"])

    def test_a_timestamp_without_a_zone_is_utc_not_local_time(self):
        """Both guests write UTC. Read as local time, a naive stamp lands hours from the ones
        beside it that do carry the `Z`, which misdates the session in the listing."""
        aware = claude_prompt_line("first ask")
        aware["timestamp"] = "2026-09-17T01:13:47.871Z"
        naive = claude_prompt_line("second ask", index=1)
        naive["timestamp"] = "2026-09-17T01:13:47.871"
        for line in (aware, naive):
            parsed, _ = self.parse([line])
            with self.subTest(timestamp=line["timestamp"]):
                self.assertAlmostEqual(1789607627.871, parsed["created"], places=3)

    def test_garbage_lines_are_skipped(self):
        path = write_claude(self.home, claude_lines())
        with open(path, "a", encoding="utf-8") as handle:
            handle.write("{not json\n[]\n")
        parsed = guest_sessions.parse_claude_transcript(path)
        self.assertEqual(CLAUDE_ID, parsed["id"])
        self.assertEqual(2, parsed["message_count"])


# ----- codex rollouts ----------------------------------------------------------------------------

class CodexParsing(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.home = self.root / "home"
        self.home.mkdir()

    def test_rollout_fields_and_counts(self):
        path = write_codex(self.home, codex_lines())
        parsed = guest_sessions.parse_codex_rollout(path)
        self.assertEqual(("codex", CODEX_ID, CWD, 2), (parsed["source"], parsed["id"],
                                                       parsed["workspace"], parsed["message_count"]))
        self.assertEqual("what does the diff view do", parsed["title"])
        self.assertEqual("prompt", parsed["title_kind"])
        self.assertEqual(["prompt", "tool_call", "reply"], [row["kind"] for row in parsed["entries"]])

    def test_developer_messages_are_not_conversation_messages(self):
        path = write_codex(self.home, codex_lines(developer=True))
        parsed = guest_sessions.parse_codex_rollout(path)
        self.assertNotIn("<skills_instructions>", json.dumps(parsed["entries"]))
        self.assertEqual(2, parsed["message_count"])

    def test_the_threads_row_names_the_session(self):
        path = write_codex(self.home, codex_lines())
        parsed = guest_sessions.parse_codex_rollout(
            path, meta={"name": "Diff view polish", "title": "ls", "workspace": CWD})
        self.assertEqual(("Diff view polish", "name"), (parsed["title"], parsed["title_kind"]))
        parsed = guest_sessions.parse_codex_rollout(
            path, meta={"name": "", "title": "ls", "workspace": CWD})
        self.assertEqual(("ls", "title"), (parsed["title"], parsed["title_kind"]))

    def test_id_and_workspace_from_the_file_name_when_the_rollout_is_empty(self):
        path = write_codex(self.home, [])
        parsed = guest_sessions.parse_codex_rollout(path)
        self.assertEqual(CODEX_ID, parsed["id"])
        self.assertEqual("", parsed["workspace"])


class CodexDatabase(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.home = self.root / "home"
        self.home.mkdir()
        (self.home / ".codex").mkdir()

    def test_reads_the_threads_row(self):
        db = write_state_db(self.home, [{"id": CODEX_ID, "rollout_path": "/x/rollout-1.jsonl",
                                         "cwd": CWD, "title": "ls", "first_user_message": "ls -la",
                                         "name": "Diff view"}], extra_columns=("originator", "recency_at"))
        meta = guest_sessions.codex_thread_meta(db)
        self.assertEqual({"workspace": CWD, "file": "/x/rollout-1.jsonl", "name": "Diff view",
                          "title": "ls", "first_user_message": "ls -la"}, meta[CODEX_ID])

    def test_an_older_schema_without_the_later_columns_still_reads(self):
        db = write_state_db(self.home, [{"id": CODEX_ID, "rollout_path": "/x/rollout-1.jsonl",
                                         "cwd": CWD, "title": "ls"}], version=3)
        self.assertEqual("ls", guest_sessions.codex_thread_meta(db)[CODEX_ID]["title"])

    def test_missing_busy_or_alien_database_is_no_metadata(self):
        self.assertEqual({}, guest_sessions.codex_thread_meta(None))
        self.assertEqual({}, guest_sessions.codex_thread_meta(self.root / "nope.sqlite"))
        alien = self.home / ".codex" / "state_9.sqlite"
        alien.write_text("not a database")
        self.assertEqual({}, guest_sessions.codex_thread_meta(alien))

    def test_a_home_whose_path_holds_uri_punctuation_still_reads(self):
        """The read-only handle is a `file:` URI. Pasted together by hand, a `?` in the path
        starts the query, a `#` starts the fragment and a `%` introduces an escape, so a user
        whose home holds any of them gets no titles at all."""
        for name in ("pct-%-home", "hash-#-home", "query-?-home", "all-%-#-?-home"):
            with self.subTest(name=name):
                home = self.root / name
                (home / ".codex").mkdir(parents=True)
                db = write_state_db(home, [{"id": CODEX_ID, "rollout_path": "/x/rollout-1.jsonl",
                                            "cwd": CWD, "title": "ls", "name": "Diff view"}])
                self.assertEqual("Diff view", guest_sessions.codex_thread_meta(db)[CODEX_ID]["name"])
                self.assertEqual("Diff view",
                                 guest_sessions.codex_thread_meta(guest.codex_state_db(str(home)))
                                 [CODEX_ID]["name"])

    def test_the_highest_state_database_wins(self):
        write_state_db(self.home, [{"id": CODEX_ID, "rollout_path": "/x/rollout-1.jsonl",
                                    "cwd": CWD, "title": "old"}], version=3)
        write_state_db(self.home, [{"id": CODEX_ID, "rollout_path": "/x/rollout-1.jsonl",
                                    "cwd": CWD, "title": "new"}], version=5)
        self.assertEqual("new", guest_sessions.codex_thread_meta(guest.codex_state_db(str(self.home)))[CODEX_ID]["title"])


# ----- claude's directory naming -----------------------------------------------------------------

class Slugs(unittest.TestCase):
    def test_slug_matches_the_directories_claude_creates(self):
        # The exact names observed under ~/.claude/projects on this machine.
        for cwd, slug in (("/home/elliott", "-home-elliott"),
                          ("/home/elliott/repos", "-home-elliott-repos"),
                          ("/home/elliott/repos/relay-terminal", "-home-elliott-repos-relay-terminal"),
                          ("/home/elliott/Dropbox/_Ash_Admin", "-home-elliott-Dropbox--Ash-Admin")):
            with self.subTest(cwd=cwd):
                self.assertEqual(slug, guest_sessions.claude_slug(cwd))

    def test_decoding_a_slug_is_a_display_fallback(self):
        self.assertEqual("/home/elliott", guest_sessions.claude_workspace_from_slug("-home-elliott"))
        self.assertEqual("", guest_sessions.claude_workspace_from_slug(""))

    def test_decoding_asks_the_filesystem_where_the_separators_were(self):
        """The encoding is lossy — a dash, an underscore and a space all become a dash — so the
        naive split turned `/home/u/repos/relay-terminal` into `/home/u/repos/relay/terminal`.
        That is not cosmetic: it becomes the row's workspace (scoping it to a project that does
        not exist) and its `resume_cwd` (so `claude -r <id>` reports an unknown session)."""
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root, True)
        (root / "home" / "u" / "repos" / "relay-terminal").mkdir(parents=True)
        (root / "home" / "u" / "Dropbox" / "_Ash_Admin").mkdir(parents=True)
        for cwd in ("/home/u/repos/relay-terminal", "/home/u/Dropbox/_Ash_Admin", "/home/u"):
            with self.subTest(cwd=cwd):
                slug = guest_sessions.claude_slug(cwd)
                self.assertEqual(str(root) + cwd,
                                 guest_sessions.claude_workspace_from_slug(slug, root=str(root)))

    def test_decoding_falls_back_when_the_directory_is_gone_or_ambiguous(self):
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root, True)
        self.assertEqual("/gone/away",
                         guest_sessions.claude_workspace_from_slug("-gone-away", root=str(root)))
        # Two real names claude spells the same way: there is no answer, so the naive one stands.
        (root / "a-b").mkdir()
        (root / "a_b").mkdir()
        self.assertEqual("/a/b", guest_sessions.claude_workspace_from_slug("-a-b", root=str(root)))


# ----- scanning a home ----------------------------------------------------------------------------

class Scanning(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.home = self.root / "home"
        self.home.mkdir()

    def test_scan_claude_reads_the_projects_tree_newest_first(self):
        old = write_claude(self.home, claude_lines(custom_title="old", cwd="/tmp/one"), cwd="/tmp/one",
                           session_id="1" * 8 + "-0000-4000-8000-000000000001")
        new = write_claude(self.home, claude_lines(custom_title="new", cwd="/tmp/two"), cwd="/tmp/two",
                           session_id="2" * 8 + "-0000-4000-8000-000000000002")
        os.utime(old, (1000, 1000))
        os.utime(new, (2000, 2000))
        records = guest_sessions.scan_claude(str(self.home))
        self.assertEqual(["new", "old"], [record["title"] for record in records])
        self.assertEqual(["/tmp/two", "/tmp/one"], [record["workspace"] for record in records])
        self.assertEqual(1, len(guest_sessions.scan_claude(str(self.home), limit=1)))
        self.assertEqual(["new"], [record["title"] for record in guest_sessions.scan_claude(str(self.home), limit=1)])

    def test_scan_claude_ignores_non_transcripts(self):
        write_claude(self.home, claude_lines())
        folder = self.home / ".claude" / "projects" / guest_sessions.claude_slug(CWD)
        (folder / "notes.txt").write_text("ignore me")
        (folder / ".hidden.jsonl").write_text("{}\n")
        self.assertEqual(1, len(guest_sessions.scan_claude(str(self.home))))

    def test_scan_codex_joins_the_threads_database(self):
        path = write_codex(self.home, codex_lines())
        write_state_db(self.home, [{"id": CODEX_ID, "rollout_path": str(path), "cwd": CWD,
                                    "title": "ls", "name": "Diff view"}])
        records = guest_sessions.scan_codex(str(self.home))
        self.assertEqual(1, len(records))
        self.assertEqual(("Diff view", CODEX_ID, CWD),
                         (records[0]["title"], records[0]["id"], records[0]["workspace"]))

    def test_scan_codex_lists_only_rollouts_that_exist(self):
        write_codex(self.home, codex_lines())
        write_state_db(self.home, [{"id": CODEX_ID, "rollout_path": "/gone/rollout.jsonl",
                                    "cwd": CWD, "title": "ls"},
                                   {"id": "9" * 8 + "-0000-4000-8000-000000000009",
                                    "rollout_path": "/gone/other.jsonl", "cwd": CWD, "title": "gone"}])
        self.assertEqual([CODEX_ID], [record["id"] for record in guest_sessions.scan_codex(str(self.home))])

    def test_scan_of_an_absent_or_unknown_guest(self):
        self.assertEqual([], guest_sessions.scan_claude(str(self.home / "nope")))
        self.assertEqual([], guest_sessions.scan_codex(str(self.home / "nope")))
        self.assertEqual([], guest_sessions.scan("claude", str(self.home)))
        with self.assertRaises(ValueError):
            guest_sessions.scan("gemini", str(self.home))

    def test_scan_skips_the_transcripts_already_indexed_at_that_mtime(self):
        path = write_claude(self.home, claude_lines())
        rollout = write_codex(self.home, codex_lines())
        stamp = path.stat().st_mtime
        self.assertEqual([], guest_sessions.scan_claude(str(self.home), known={CLAUDE_ID: stamp}))
        self.assertEqual(1, len(guest_sessions.scan_claude(str(self.home), known={CLAUDE_ID: stamp + 1})))
        # codex's id comes from the rollout's name, not from its lines, so the skip works there too.
        self.assertEqual([], guest_sessions.scan_codex(str(self.home),
                                                       known={CODEX_ID: rollout.stat().st_mtime}))
        self.assertEqual(1, len(guest_sessions.scan_codex(str(self.home), known={CODEX_ID: 0.0})))
        # An unknown mtime (`None`, or no row at all) is always read.
        self.assertEqual(1, len(guest_sessions.scan_codex(str(self.home), known={CODEX_ID: None})))
        self.assertEqual(1, len(guest_sessions.scan_codex(str(self.home), known={})))


# ----- the index cache -----------------------------------------------------------------------------

class IndexIntegration(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.home = self.root / "home"
        self.home.mkdir()
        self.claude = write_claude(self.home, claude_lines())
        self.rollout = write_codex(self.home, codex_lines(prompt="how do I search the pelican index"))
        write_state_db(self.home, [{"id": CODEX_ID, "rollout_path": str(self.rollout), "cwd": CWD,
                                    "title": "pelican index"}])
        self.index = index_in(self.root)

    def tearDown(self):
        self.index.close()

    def test_reconcile_indexes_both_sources(self):
        result = guest_sessions.reconcile(self.index, str(self.home))
        self.assertEqual((2, 0, 0), (result["added"], result["refreshed"], result["removed"]))
        records = guest_sessions.list_sessions(self.index)
        self.assertEqual({"claude", "codex"}, {record["source"] for record in records})
        by_source = {record["source"]: record for record in records}
        self.assertEqual(["claude", "-r", CLAUDE_ID], by_source["claude"]["resume_command"])
        self.assertEqual(["codex", "resume", CODEX_ID], by_source["codex"]["resume_command"])
        self.assertEqual(CWD, by_source["codex"]["workspace"])
        self.assertEqual("pelican index", by_source["codex"]["title"])
        self.assertEqual(2, by_source["codex"]["message_count"])

    def test_reconcile_reads_only_what_changed(self):
        guest_sessions.reconcile(self.index, str(self.home))
        again = guest_sessions.reconcile(self.index, str(self.home))
        self.assertEqual((0, 0, 0), (again["added"], again["refreshed"], again["removed"]))
        os.utime(self.claude, None)
        touched = guest_sessions.reconcile(self.index, str(self.home))
        self.assertEqual((0, 1, 0), (touched["added"], touched["refreshed"], touched["removed"]))

    def test_reconcile_does_not_read_a_transcript_whose_mtime_is_unchanged(self):
        """The skip is by mtime: content rewritten under a kept mtime is not read, and a touch
        brings it in — which is what keeps the second reconcile of a long history cheap."""
        guest_sessions.reconcile(self.index, str(self.home))
        stamp = self.claude.stat().st_mtime
        self.claude.write_text("".join(json.dumps(line) + "\n" for line in claude_lines(
            custom_title="rewritten", replies=())), encoding="utf-8")
        os.utime(self.claude, (stamp, stamp))
        again = guest_sessions.reconcile(self.index, str(self.home))
        self.assertEqual((0, 0, 0), (again["added"], again["refreshed"], again["removed"]))
        self.assertEqual("pane-drag-fix",
                         {record["id"]: record["title"]
                          for record in guest_sessions.list_sessions(self.index)}[CLAUDE_ID])
        os.utime(self.claude, None)
        touched = guest_sessions.reconcile(self.index, str(self.home))
        self.assertEqual((0, 1, 0), (touched["added"], touched["refreshed"], touched["removed"]))
        self.assertEqual("rewritten",
                         {record["id"]: record["title"]
                          for record in guest_sessions.list_sessions(self.index)}[CLAUDE_ID])

    def test_a_limited_reconcile_never_drops_the_rows_it_did_not_look_at(self):
        """`limit` caps the scan at the N newest transcripts per source. Those are the only
        files it stats, so it cannot tell a session that went away from one it simply did not
        read: pruning on that would leave the pane holding the newest N and nothing else."""
        for index in range(5):
            path = write_claude(self.home, claude_lines(custom_title=f"session {index}"),
                                session_id=f"{index}" * 8 + "-0000-4000-8000-00000000000" + str(index))
            os.utime(path, (1000 + index, 1000 + index))
        first = guest_sessions.reconcile(self.index, str(self.home))
        self.assertEqual((7, 0, 0), (first["added"], first["refreshed"], first["removed"]))
        capped = guest_sessions.reconcile(self.index, str(self.home), limit=2)
        self.assertEqual(0, capped["removed"], "a capped scan may not prune")
        self.assertEqual(7, len(guest_sessions.list_sessions(self.index)))
        # A full reconcile still prunes: that is the run that stats every file.
        self.claude.unlink()
        self.assertEqual(1, guest_sessions.reconcile(self.index, str(self.home))["removed"])
        self.assertEqual(6, len(guest_sessions.list_sessions(self.index)))

    def test_a_transcript_whose_id_line_disagrees_is_still_skipped_next_time(self):
        """The row is keyed by the id the file's name carries, which is the id `_walk` looks a
        file up by before parsing it. Keyed by an in-file `sessionId` that disagrees, the skip
        would never fire and every reconcile would re-read the whole transcript."""
        named = "5" * 8 + "-0000-4000-8000-000000000005"
        write_claude(self.home, claude_lines(session_id="not-the-file-name", custom_title="odd one"),
                     session_id=named)
        guest_sessions.reconcile(self.index, str(self.home))
        self.assertIn(named, [record["id"] for record in guest_sessions.list_sessions(self.index)])
        again = guest_sessions.reconcile(self.index, str(self.home))
        self.assertEqual((0, 0, 0), (again["added"], again["refreshed"], again["removed"]))

    def test_a_source_whose_directory_is_gone_prunes_nothing(self):
        """`$HOME` can be wrong, a network home can be late, a guest can be uninstalled with its
        history intact. Reading "no files" as "every session was deleted" emptied the pane and
        took the pins and the custom titles with it — and a guest row has no file beside it to
        restore either from."""
        guest_sessions.reconcile(self.index, str(self.home))
        shutil.rmtree(self.home / ".claude")
        result = guest_sessions.reconcile(self.index, str(self.home))
        self.assertEqual(0, result["removed"])
        self.assertIn(CLAUDE_ID, [record["id"] for record in guest_sessions.list_sessions(self.index)])
        # A directory that is there and empty is a different statement, and it does prune.
        (self.home / ".claude" / "projects").mkdir(parents=True)
        self.assertEqual(1, guest_sessions.reconcile(self.index, str(self.home))["removed"])

    def test_a_transcript_that_cannot_be_read_keeps_its_row(self):
        """A transient permission or IO failure is not a deleted session."""
        guest_sessions.reconcile(self.index, str(self.home))
        before = self.claude.stat().st_mode
        os.utime(self.claude, (3000, 3000))          # changed, so it is not skipped as unchanged
        self.claude.chmod(0o000)
        self.addCleanup(self.claude.chmod, before)
        if os.access(self.claude, os.R_OK):
            self.skipTest("running as root: an unreadable file cannot be made")
        result = guest_sessions.reconcile(self.index, str(self.home))
        self.assertEqual(0, result["removed"])
        self.assertIn(CLAUDE_ID, [record["id"] for record in guest_sessions.list_sessions(self.index)])

    def test_a_limit_of_zero_reads_nothing_and_prunes_nothing(self):
        """`[:limit or None]` read *everything* at zero and dropped the oldest file at -1."""
        guest_sessions.reconcile(self.index, str(self.home))
        os.utime(self.claude, (4000, 4000))
        for limit in (0, -1):
            with self.subTest(limit=limit):
                result = guest_sessions.reconcile(self.index, str(self.home), limit=limit)
                self.assertEqual((0, 0, 0), (result["added"], result["refreshed"], result["removed"]))
        self.assertEqual(2, len(guest_sessions.list_sessions(self.index)))

    def test_only_the_named_source_is_pruned(self):
        guest_sessions.reconcile(self.index, str(self.home))
        self.rollout.unlink()
        self.assertEqual(0, guest_sessions.reconcile(self.index, str(self.home),
                                                     sources=("claude",))["removed"])
        self.assertEqual(1, guest_sessions.reconcile(self.index, str(self.home),
                                                     sources=("codex",))["removed"])

    def test_what_a_guest_row_puts_in_relays_index(self):
        """The privacy surface, pinned: indexing a guest session copies its prompts and replies
        into Relay's own database (`entries`, and the FTS index built off it), which is what makes
        the sessions pane searchable. Nothing else of the transcript is kept — no file path, no
        model, no tool output — and nothing is ever written back to the guest's files."""
        guest_sessions.reconcile(self.index, str(self.home))
        rows = self.index._run(lambda db: [dict(row) for row in db.execute(
            "SELECT kind, text FROM entries WHERE session_id = ? ORDER BY seq", (CLAUDE_ID,))])
        self.assertEqual(["title", "prompt", "reply", "tool_call"], [row["kind"] for row in rows])
        by_kind = {row["kind"]: row["text"] for row in rows}
        self.assertEqual("fix the pane drag", by_kind["prompt"])       # verbatim, up to MAX_PROMPT
        self.assertEqual("Fixed it in Pane.h.", by_kind["reply"])      # verbatim, up to MAX_TEXT
        self.assertTrue(by_kind["tool_call"].startswith("Edit "))
        self.assertTrue(self.claude.is_file(), "the transcript itself is never touched")

    def test_reconcile_drops_the_row_but_never_the_transcript(self):
        guest_sessions.reconcile(self.index, str(self.home))
        self.rollout.unlink()
        result = guest_sessions.reconcile(self.index, str(self.home))
        self.assertEqual(1, result["removed"])
        self.assertEqual([CLAUDE_ID], [record["id"] for record in guest_sessions.list_sessions(self.index)])
        self.assertTrue(self.claude.is_file())

    def test_search_spans_the_guest_sources(self):
        guest_sessions.reconcile(self.index, str(self.home))
        found = guest_sessions.search_sessions(self.index, "pelican")
        self.assertEqual(["codex"], [item["source"] for item in found["items"]])
        self.assertEqual(["codex", "resume", CODEX_ID], found["items"][0]["resume_command"])
        self.assertTrue(found["items"][0]["matches"])
        # One query over both guest sources returns whichever guest matches.
        claude = guest_sessions.search_sessions(self.index, "pane", sources=("claude", "codex"))
        self.assertEqual(["claude"], [item["source"] for item in claude["items"]])
        # An empty query lists both, so the pane's unfiltered view spans the guests too.
        listed = guest_sessions.search_sessions(self.index, "", sources=("claude", "codex"))
        self.assertEqual({"claude", "codex"}, {item["source"] for item in listed["items"]})

    def test_a_guest_session_is_not_listed_unless_asked_for(self):
        guest_sessions.reconcile(self.index, str(self.home))
        self.assertEqual([], self.index.search("pelican")["items"])
        self.assertEqual([], self.index.search("pelican", sources=["claude"])["items"])
        self.assertEqual(1, len(self.index.search("pelican", sources=["codex"], scope="all")["items"]))
        # A guest row is workspace-scoped like any other, so the pane's own project filter
        # still applies to it.
        self.assertEqual(1, len(self.index.search("pelican", sources=["codex"], scope="project",
                                                  workspace=CWD)["items"]))
        self.assertEqual([], self.index.search("pelican", sources=["codex"], scope="project",
                                              workspace="/tmp/elsewhere")["items"])
        self.assertEqual(("agent", "terminal", "subagent", "claude", "codex"), conv_index.SOURCES)
        self.assertEqual(("claude", "codex"), conv_index.GUEST_SOURCES)

    def test_fork_spells_the_resume_command_differently(self):
        guest_sessions.reconcile(self.index, str(self.home))
        records = {record["source"]: record for record in guest_sessions.list_sessions(self.index, fork=True)}
        self.assertEqual(["claude", "-r", CLAUDE_ID, "--fork-session"], records["claude"]["resume_command"])
        self.assertEqual(["codex", "fork", CODEX_ID], records["codex"]["resume_command"])

    def test_rebuild_keeps_the_guest_rows(self):
        guest_sessions.reconcile(self.index, str(self.home))
        self.index.rebuild(str(self.root / "sessions"))
        self.assertEqual(2, len(guest_sessions.list_sessions(self.index)))

    def test_a_guest_row_carries_no_session_dir_and_survives_delete_files_false(self):
        guest_sessions.reconcile(self.index, str(self.home))
        row = self.index.search("", sources=["claude"], scope="all")["items"][0]
        self.assertEqual("", row["session_dir"])
        removed = self.index.delete_session(CLAUDE_ID, remove_files=False)
        self.assertTrue(removed["indexed"])
        self.assertTrue(self.claude.is_file())

    def test_update_guest_rejects_a_bad_source_and_a_missing_id(self):
        with self.assertRaises(ValueError):
            self.index.update_guest({"source": "agent", "id": CLAUDE_ID})
        with self.assertRaises(ValueError):
            self.index.update_guest({"source": "claude", "id": ""})

    def test_a_rename_survives_a_reindex(self):
        guest_sessions.reconcile(self.index, str(self.home))
        self.index.rename(CLAUDE_ID, "my pane session")
        os.utime(self.claude, None)
        guest_sessions.reconcile(self.index, str(self.home))
        titles = {record["id"]: record["title"] for record in guest_sessions.list_sessions(self.index)}
        self.assertEqual("my pane session", titles[CLAUDE_ID])

    def user_fields(self, session_id=CLAUDE_ID) -> tuple[str, int]:
        row = self.index.search("", sources=["claude", "codex"], scope="all")["items"]
        item = {entry["session_id"]: entry for entry in row}[session_id]
        return item["title"], item["pinned"]

    def test_update_guest_merges_the_user_fields_one_key_at_a_time(self):
        """`custom_title` and `pinned` are independent: naming a session must not unpin it and
        pinning one must not throw its name away."""
        guest_sessions.reconcile(self.index, str(self.home))
        parsed = guest_sessions.parse_claude_transcript(self.claude)
        self.index.rename(CLAUDE_ID, "my pane session")
        self.index.set_pinned(CLAUDE_ID, True)
        # A writer that mentions only the pin keeps the name.
        self.index.update_guest({**parsed, "pinned": True})
        self.assertEqual(("my pane session", 1), self.user_fields())
        # A writer that mentions only the name keeps the pin.
        self.index.update_guest({**parsed, "custom_title": "renamed again"})
        self.assertEqual(("renamed again", 1), self.user_fields())
        # And each of them can still clear its own field.
        self.index.update_guest({**parsed, "pinned": False})
        self.assertEqual(("renamed again", 0), self.user_fields())
        self.index.update_guest({**parsed, "custom_title": ""})
        self.assertEqual(("pane-drag-fix", 0), self.user_fields())

    def test_update_guest_without_either_key_keeps_both(self):
        guest_sessions.reconcile(self.index, str(self.home))
        self.index.rename(CLAUDE_ID, "my pane session")
        self.index.set_pinned(CLAUDE_ID, True)
        os.utime(self.claude, None)
        guest_sessions.reconcile(self.index, str(self.home))
        self.assertEqual(("my pane session", 1), self.user_fields())


# ----- the live tail -------------------------------------------------------------------------------

class LiveTail(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.home = self.root / "home"
        self.home.mkdir()

    def append(self, path: Path, lines) -> None:
        with open(path, "a", encoding="utf-8") as handle:
            for line in lines:
                handle.write(json.dumps(line) + "\n")
            handle.flush()
            os.fsync(handle.fileno())

    def test_a_running_claude_session_appears_and_grows(self):
        path = write_claude(self.home, claude_lines(prompts=("first ask",), replies=()))
        tail = guest_sessions.LiveTail(path, home=str(self.home))
        record = tail.record()
        self.assertEqual(("claude", CLAUDE_ID, 1), (record["source"], record["id"], record["message_count"]))
        self.assertEqual(["claude", "-r", CLAUDE_ID], record["resume_command"])
        # The guest keeps writing: a reply, then a second prompt.
        self.append(path, [claude_reply_line("an answer", tool_use=False),
                           claude_prompt_line("second ask", index=1)])
        record = tail.refresh()
        self.assertEqual(3, record["message_count"])
        self.assertEqual(["prompt", "reply", "prompt"], [row["kind"] for row in tail.parsed["entries"]])
        self.assertEqual("pane-drag-fix", record["title"])

    def test_a_partial_line_waits_for_its_newline(self):
        path = write_claude(self.home, claude_lines(prompts=("first ask",), replies=()))
        tail = guest_sessions.LiveTail(path, home=str(self.home))
        before = tail.record()["message_count"]
        partial = json.dumps(claude_prompt_line("half written"))
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(partial)                       # no newline yet
        self.assertEqual(before, tail.refresh()["message_count"])
        self.assertEqual(before, tail.refresh()["message_count"])   # still nothing new
        with open(path, "a", encoding="utf-8") as handle:
            handle.write("\n")                          # the guest finishes the line
        self.assertEqual(before + 1, tail.refresh()["message_count"])

    def test_a_truncated_transcript_is_read_again(self):
        path = write_claude(self.home, claude_lines(prompts=("first ask", "second ask"), replies=()))
        tail = guest_sessions.LiveTail(path, home=str(self.home))
        self.assertEqual(2, tail.record()["message_count"])
        path.write_text("".join(json.dumps(line) + "\n"
                                for line in claude_lines(prompts=("only ask",), replies=())),
                        encoding="utf-8")
        self.assertEqual(1, tail.refresh()["message_count"])

    def test_a_running_codex_session_takes_its_name_from_the_database(self):
        path = write_codex(self.home, codex_lines(prompt="how do I search the pelican index"))
        write_state_db(self.home, [{"id": CODEX_ID, "rollout_path": str(path), "cwd": CWD,
                                    "title": "ls", "name": "Pelican search"}])
        tail = guest_sessions.LiveTail(path, home=str(self.home))
        record = tail.record()
        self.assertEqual(("codex", "Pelican search", 2),
                         (record["source"], record["title"], record["message_count"]))
        self.append(path, [codex_reply_line("a second answer")])
        self.assertEqual(3, tail.refresh()["message_count"])

    def test_the_codex_threads_database_is_read_once_per_interval(self):
        """Reading it is a fresh connection, a `PRAGMA table_info` and a `SELECT` over every
        thread the user ever had. The pane refreshes as fast as the guest writes, so the tail
        caches it; a thread's name changes about once a session."""
        path = write_codex(self.home, codex_lines())
        write_state_db(self.home, [{"id": CODEX_ID, "rollout_path": str(path), "cwd": CWD,
                                    "title": "ls", "name": "Pelican search"}])
        real, reads = guest_sessions.codex_thread_meta, []

        def counted(db_path):
            reads.append(db_path)
            return real(db_path)

        guest_sessions.codex_thread_meta = counted
        try:
            tail = guest_sessions.LiveTail(path, home=str(self.home))
            self.assertEqual("Pelican search", tail.record()["title"])
            self.append(path, [codex_reply_line("a second answer")])
            self.assertEqual(3, tail.refresh()["message_count"])
            self.append(path, [codex_reply_line("a third answer")])
            self.assertEqual(4, tail.refresh()["message_count"])
            self.assertEqual(1, len(reads), "three parses, one read of the database")
            self.assertEqual("Pelican search", tail.record()["title"], "still named from the cache")
            # Past the interval the tail picks a rename up again.
            tail._meta_at -= guest_sessions.LiveTail.META_REFRESH
            write_state_db(self.home, [{"id": CODEX_ID, "rollout_path": str(path), "cwd": CWD,
                                        "title": "ls", "name": "Renamed thread"}], version=6)
            self.append(path, [codex_reply_line("a fourth answer")])
            self.assertEqual("Renamed thread", tail.refresh()["title"])
            self.assertEqual(2, len(reads))
        finally:
            guest_sessions.codex_thread_meta = real

    def test_a_tail_of_a_missing_file_is_empty(self):
        tail = guest_sessions.LiveTail(self.root / "gone.jsonl", home=str(self.home))
        self.assertIsNone(tail.record())
        self.assertIsNone(tail.refresh())

    def test_live_transcript_picks_the_newest_for_a_workspace(self):
        old = write_claude(self.home, claude_lines(custom_title="old"))
        new = write_claude(self.home, claude_lines(custom_title="new"),
                           session_id="3" * 8 + "-0000-4000-8000-000000000003")
        os.utime(old, (1000, 1000))
        os.utime(new, (2000, 2000))
        self.assertEqual(new, guest_sessions.claude_live_transcript(CWD, str(self.home)))
        self.assertEqual(new, guest_sessions.live_transcript("claude", CWD, str(self.home)))
        self.assertIsNone(guest_sessions.claude_live_transcript("/tmp/nowhere", str(self.home)))

    def test_codex_live_transcript_matches_the_workspace(self):
        path = write_codex(self.home, codex_lines())
        write_state_db(self.home, [{"id": CODEX_ID, "rollout_path": str(path), "cwd": CWD, "title": "ls"}])
        self.assertEqual(path, guest_sessions.codex_live_transcript(CWD, str(self.home)))
        self.assertEqual(path, guest_sessions.live_transcript("codex", CWD, str(self.home)))
        self.assertIsNone(guest_sessions.codex_live_transcript("/tmp/nowhere", str(self.home)))
        with self.assertRaises(ValueError):
            guest_sessions.live_transcript("gemini", CWD, str(self.home))

    def test_guess_source_by_file_name(self):
        self.assertEqual("codex", guest_sessions.guess_source(CODEX_NAME))
        self.assertEqual("claude", guest_sessions.guess_source(f"{CLAUDE_ID}.jsonl"))


# ----- the opt-out, the forgotten set and the meta store (GT7X review) ------------------------------

class GuestIndexingSetting(unittest.TestCase):
    """`RELAY_INDEX` turns the whole index off; this is the narrower switch behind the GUI's
    "index my other agents' sessions", which may leave Relay's own search working."""

    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.home = self.root / "home"
        self.home.mkdir()
        self.claude = write_claude(self.home, claude_lines())
        self.rollout = write_codex(self.home, codex_lines())
        self.index = index_in(self.root)
        self.addCleanup(self.index.close)

    def test_switched_off_the_guests_files_are_not_read_and_the_rows_go(self):
        guest_sessions.reconcile(self.index, str(self.home))
        self.assertEqual(2, len(guest_sessions.list_sessions(self.index)))
        outcome = guest_sessions.reconcile(self.index, str(self.home), enabled=False)
        self.assertEqual((0, 0, 2, True), (outcome["added"], outcome["refreshed"], outcome["removed"],
                                           outcome["skipped"]))
        self.assertEqual([], guest_sessions.list_sessions(self.index))
        self.assertEqual([], guest_sessions.search_sessions(self.index, "pane")["items"])
        # And it stays that way however often the worker asks.
        self.assertEqual(0, guest_sessions.reconcile(self.index, str(self.home), enabled=False)["removed"])
        self.assertEqual([], guest_sessions.list_sessions(self.index))
        # The guests' own files are the one thing this may never touch.
        self.assertTrue(self.claude.is_file() and self.rollout.is_file())

    def test_switched_on_again_the_rows_come_back_with_the_pins(self):
        guest_sessions.reconcile(self.index, str(self.home))
        self.index.rename(CLAUDE_ID, "kept name")
        self.index.set_pinned(CLAUDE_ID, True)
        guest_sessions.reconcile(self.index, str(self.home), enabled=False)
        guest_sessions.reconcile(self.index, str(self.home))
        rows = {record["id"]: record for record in guest_sessions.list_sessions(self.index)}
        self.assertEqual(2, len(rows))
        self.assertEqual("kept name", rows[CLAUDE_ID]["title"])
        item = self.index.search("", scope="all", sources=["claude"])["items"][0]
        self.assertEqual(1, item["pinned"])

    def test_purge_is_the_same_thing_on_its_own(self):
        guest_sessions.reconcile(self.index, str(self.home))
        self.assertEqual(2, guest_sessions.purge(self.index))
        self.assertEqual(0, guest_sessions.purge(self.index))
        self.assertEqual([], guest_sessions.list_sessions(self.index))

    def test_only_the_named_source_is_purged(self):
        guest_sessions.reconcile(self.index, str(self.home))
        self.assertEqual(1, guest_sessions.purge(self.index, sources=["codex"]))
        self.assertEqual(["claude"], [r["source"] for r in guest_sessions.list_sessions(self.index)])

    def test_the_environment_variable_is_the_default(self):
        with mock.patch.dict(os.environ, {"RELAY_INDEX_GUESTS": "off"}):
            self.assertFalse(guest_sessions.guests_enabled())
            self.assertTrue(guest_sessions.reconcile(self.index, str(self.home))["skipped"])
        self.assertTrue(guest_sessions.guests_enabled())
        self.assertNotIn("skipped", guest_sessions.reconcile(self.index, str(self.home)))


class ForgottenSessions(unittest.TestCase):
    """Deleting a guest row has to stick: the transcript is the guest's and stays, so the next
    reconcile reads the same file and finds the same session."""

    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.home = self.root / "home"
        self.home.mkdir()
        self.claude = write_claude(self.home, claude_lines())
        self.index = index_in(self.root)
        self.addCleanup(self.index.close)
        guest_sessions.reconcile(self.index, str(self.home))

    def test_a_deleted_guest_session_does_not_come_back(self):
        removed = self.index.delete_session(CLAUDE_ID, remove_files=False)
        self.assertTrue(removed["forgotten"])
        self.assertEqual({("claude", CLAUDE_ID)}, self.index.forgotten())
        outcome = guest_sessions.reconcile(self.index, str(self.home))
        self.assertEqual((0, 0, 0), (outcome["added"], outcome["refreshed"], outcome["removed"]))
        self.assertEqual([], guest_sessions.list_sessions(self.index))
        self.assertEqual([], guest_sessions.search_sessions(self.index, "pane")["items"])
        self.assertTrue(self.claude.is_file(), "the guest's own transcript is never deleted")

    def test_update_guest_itself_refuses_a_forgotten_session(self):
        """The reconcile does not even read the file, but the tail and any other writer come
        through `update_guest`, so the refusal lives there too."""
        self.index.delete_session(CLAUDE_ID, remove_files=False)
        parsed = guest_sessions.parse_claude_transcript(self.claude)
        self.assertEqual(0, self.index.update_guest(parsed))
        self.assertEqual([], guest_sessions.list_sessions(self.index))

    def test_unforget_brings_it_back_with_the_name_it_had(self):
        self.index.rename(CLAUDE_ID, "the one I deleted")
        self.index.delete_session(CLAUDE_ID, remove_files=False)
        self.index.unforget("claude", CLAUDE_ID)
        self.assertEqual(set(), self.index.forgotten())
        self.assertEqual(1, guest_sessions.reconcile(self.index, str(self.home))["added"])
        self.assertEqual(["the one I deleted"],
                         [record["title"] for record in guest_sessions.list_sessions(self.index)])

    def test_a_row_pruned_because_its_file_is_gone_is_not_forgotten(self):
        """A transcript that went away is not the user saying "never show me this again": a home
        that was late, or a restored backup, must bring the session back."""
        moved = self.claude.with_suffix(".away")
        self.claude.rename(moved)
        self.assertEqual(1, guest_sessions.reconcile(self.index, str(self.home))["removed"])
        self.assertEqual(set(), self.index.forgotten())
        moved.rename(self.claude)
        self.assertEqual(1, guest_sessions.reconcile(self.index, str(self.home))["added"])

    def test_forgetting_is_per_source_and_ignores_anything_else(self):
        self.index.forget("codex", CLAUDE_ID)
        self.index.forget("gemini", CLAUDE_ID)              # not a guest: nothing recorded
        self.index.forget("claude", "")
        self.assertEqual({("codex", CLAUDE_ID)}, self.index.forgotten())
        self.assertEqual(1, len(guest_sessions.list_sessions(self.index)), "the claude row stays")


class MetaStoreSurvival(unittest.TestCase):
    """A guest row has no `<id>.meta.json` beside it, so the pin, the name and the forgotten set
    live in `guest-meta.json` beside the index — the one thing here that outlives the database."""

    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.home = self.root / "home"
        self.home.mkdir()
        write_claude(self.home, claude_lines())
        write_codex(self.home, codex_lines())
        self.index = index_in(self.root)
        self.addCleanup(lambda: self.index.close())
        guest_sessions.reconcile(self.index, str(self.home))

    def reopen(self):
        """Throw the database away, as a schema bump or a corruption does, and reconcile again."""
        self.index.close()
        for suffix in ("", "-wal", "-shm"):
            Path(str(self.root / "index.db") + suffix).unlink(missing_ok=True)
        self.index = index_in(self.root)
        guest_sessions.reconcile(self.index, str(self.home))

    def test_a_pin_and_a_name_survive_the_database_being_discarded(self):
        self.index.rename(CLAUDE_ID, "the pane drag one")
        self.index.set_pinned(CLAUDE_ID, True)
        self.reopen()
        rows = {row["session_id"]: row for row in
                self.index.search("", scope="all", sources=list(GUEST_SOURCES))["items"]}
        item = rows[CLAUDE_ID]
        self.assertEqual("the pane drag one", item["title"])
        self.assertEqual(1, item["pinned"])
        # Searchable under the name the user gave it, not only listed under it.
        self.assertEqual([CLAUDE_ID], [item["session_id"] for item in
                                       self.index.search("pane drag one", scope="all",
                                                         sources=list(GUEST_SOURCES))["items"]])

    def test_a_deletion_survives_it_too(self):
        self.index.delete_session(CLAUDE_ID, remove_files=False)
        self.reopen()
        self.assertEqual({("claude", CLAUDE_ID)}, self.index.forgotten())
        self.assertEqual([CODEX_ID], [record["id"] for record in guest_sessions.list_sessions(self.index)])

    def test_unpinning_and_renaming_back_empty_the_store_again(self):
        self.index.set_pinned(CLAUDE_ID, True)
        self.index.rename(CLAUDE_ID, "a name")
        self.index.set_pinned(CLAUDE_ID, False)
        self.index.rename(CLAUDE_ID, "")
        self.assertEqual({}, self.index.guest_meta("claude", CLAUDE_ID))
        self.reopen()
        rows = {record["id"]: record for record in guest_sessions.list_sessions(self.index)}
        self.assertEqual("pane-drag-fix", rows[CLAUDE_ID]["title"], "back to the guest's own title")

    def test_a_corrupt_store_is_an_empty_one_and_is_rewritten(self):
        self.index.store_path.write_text("{not json at all", encoding="utf-8")
        self.assertEqual({}, self.index.guest_meta())
        self.index.set_pinned(CLAUDE_ID, True)
        self.assertTrue(conv_index.read_guest_meta(self.index.store_path)["claude"][CLAUDE_ID]["pinned"])
        self.assertEqual(0o600, self.index.store_path.stat().st_mode & 0o777)

    def test_only_a_guest_row_reaches_the_store(self):
        self.index.rename("term-" + "0" * 16, "terminal history")
        self.assertEqual({}, self.index.guest_meta())


# ----- the directory a guest resumes in (GT7X review, B4) -------------------------------------------

class RawWorkingDirectory(unittest.TestCase):
    """`resume_cwd` is the cwd the transcript names, not the resolved one.

    The index resolves a workspace (`normalize_workspace` → `Path.resolve()`) so the same project
    reached two ways groups as one. That is right for grouping and wrong for resuming: claude
    files a transcript under the slug of the directory it was *started* in, so a workspace reached
    through a symlink resumes in a real directory claude has no `<cwd-slug>` for, and
    `claude -r <id>` reports an unknown session there.
    """

    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.home = self.root / "home"
        self.home.mkdir()
        self.real = self.root / "real-project"
        self.real.mkdir()
        self.link = self.root / "linked-project"
        self.link.symlink_to(self.real, target_is_directory=True)
        self.index = index_in(self.root)
        self.addCleanup(self.index.close)

    def test_the_record_and_the_row_both_resume_where_the_transcript_says(self):
        write_claude(self.home, claude_lines(cwd=str(self.link)), cwd=str(self.link))
        parsed = guest_sessions.scan_claude(str(self.home))[0]
        self.assertEqual(str(self.link), parsed["raw_cwd"])
        self.assertEqual(str(self.link), guest_sessions.to_record(parsed)["resume_cwd"])

        guest_sessions.reconcile(self.index, str(self.home))
        record = guest_sessions.list_sessions(self.index)[0]
        self.assertEqual(str(self.link), record["resume_cwd"], "the directory claude knows it by")
        self.assertEqual(str(self.real), record["workspace"], "resolved, for grouping and filters")
        item = self.index.search("", scope="all", sources=["claude"])["items"][0]
        self.assertEqual((str(self.real), str(self.link)), (item["workspace"], item["raw_cwd"]))
        annotated = guest_sessions.annotate_items([item])[0]
        self.assertEqual(str(self.link), annotated["resume_cwd"])
        self.assertEqual({"argv": ["claude", "-r", CLAUDE_ID], "cwd": str(self.link)},
                         guest_sessions.resume_spawn("claude", CLAUDE_ID, item["workspace"],
                                                     raw_cwd=item["raw_cwd"]))
        # Scoping by the project still finds it: that filter is on the resolved path.
        self.assertEqual(1, len(guest_sessions.list_sessions(self.index, scope="project",
                                                             workspace=str(self.link))))

    def test_a_codex_rollout_keeps_its_own_cwd_too(self):
        path = write_codex(self.home, codex_lines(cwd=str(self.link)))
        write_state_db(self.home, [{"id": CODEX_ID, "rollout_path": str(path), "cwd": str(self.link)}])
        guest_sessions.reconcile(self.index, str(self.home))
        record = guest_sessions.list_sessions(self.index)[0]
        self.assertEqual((str(self.link), str(self.real)), (record["resume_cwd"], record["workspace"]))

    def test_without_a_cwd_line_the_project_directory_is_the_raw_one(self):
        """claude's own directory name is its spelling of the cwd it was started in, so it is a
        raw cwd as well — and the only one a transcript that never wrote a `cwd` line has."""
        write_claude(self.home, [], cwd=str(self.link))
        parsed = guest_sessions.scan_claude(str(self.home))[0]
        self.assertEqual(str(self.link), parsed["raw_cwd"])
        self.assertEqual(str(self.link), guest_sessions.to_record(parsed)["resume_cwd"])

    def test_a_row_indexed_before_the_column_existed_still_resumes(self):
        """`raw_cwd` is empty for every row a v3 index wrote; the resolved workspace is what those
        had, and it is what they go on using until the next reconcile fills the column in."""
        self.assertEqual(CWD, guest_sessions.item_to_record(
            {"source": "claude", "session_id": CLAUDE_ID, "workspace": CWD})["resume_cwd"])


# ----- an incremental reconcile (GT7X review, B5) ---------------------------------------------------

class IncrementalReconcile(unittest.TestCase):
    """jsonl is append-only, so a transcript that only grew is read from where the last run
    stopped. Before this, any mtime change re-parsed the whole file and rewrote every entry —
    the owner's largest transcript is 99 MB and caps out at 20 000 entries."""

    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.home = self.root / "home"
        self.home.mkdir()
        self.path = write_claude(self.home, claude_lines(prompts=("first ask",), replies=("one answer",)))
        self.index = index_in(self.root)
        self.addCleanup(self.index.close)
        guest_sessions.reconcile(self.index, str(self.home))

    def append(self, lines) -> None:
        with open(self.path, "a", encoding="utf-8") as handle:
            for line in lines:
                handle.write(json.dumps(line) + "\n")
        os.utime(self.path, None)

    def entry_ids(self) -> list[int]:
        """The rowids of the session's entries, straight out of the database: they change only if
        the entries were deleted and written again."""
        db = sqlite3.connect(str(self.root / "index.db"))
        try:
            return [row[0] for row in db.execute(
                "SELECT id FROM entries WHERE session_id=? ORDER BY id", (CLAUDE_ID,)).fetchall()]
        finally:
            db.close()

    def test_appended_lines_are_appended_not_rewritten(self):
        before = self.entry_ids()
        self.append([claude_prompt_line("second ask", index=1),
                     claude_reply_line("two answer", index=1, tool_use=False)])
        outcome = guest_sessions.reconcile(self.index, str(self.home))
        self.assertEqual((0, 1, 0), (outcome["added"], outcome["refreshed"], outcome["removed"]))
        after = self.entry_ids()
        self.assertEqual(before, after[:len(before)], "the entries already indexed kept their rows")
        self.assertEqual(len(before) + 2, len(after))
        entries = self.index.conversation(CLAUDE_ID)["items"]
        self.assertEqual(["prompt", "reply", "tool_call", "prompt", "reply"],
                         [entry["kind"] for entry in entries])
        self.assertEqual([1, 1, 1, 2, 2], [entry["turn"] for entry in entries])
        self.assertEqual(4, self.index.search("", scope="all", sources=["claude"])["items"][0]["turns"])
        self.assertEqual([CLAUDE_ID], [item["session_id"] for item in
                                       self.index.search("two answer", scope="all",
                                                         sources=["claude"])["items"]])

    def test_the_parse_carries_on_where_it_stopped(self):
        """Everything the first part of the file said — the workspace, the created stamp, the
        title, the counts — is still the record's after an append that says none of it."""
        first = guest_sessions.list_sessions(self.index)[0]
        self.append([claude_prompt_line("second ask", index=1)])
        guest_sessions.reconcile(self.index, str(self.home))
        again = guest_sessions.list_sessions(self.index)[0]
        self.assertEqual((first["workspace"], first["title"]), (again["workspace"], again["title"]))
        self.assertEqual(first["message_count"] + 1, again["message_count"])

    def test_a_shorter_file_is_read_from_the_start_again(self):
        self.path.write_text("".join(json.dumps(line) + "\n" for line in
                                     claude_lines(prompts=("rewritten",), replies=(),
                                                  custom_title="compacted")), encoding="utf-8")
        os.utime(self.path, None)
        guest_sessions.reconcile(self.index, str(self.home))
        entries = self.index.conversation(CLAUDE_ID)["items"]
        self.assertEqual(["prompt"], [entry["kind"] for entry in entries])
        self.assertEqual("rewritten", entries[0]["text"])
        self.assertEqual("compacted", guest_sessions.list_sessions(self.index)[0]["title"])

    def test_a_rewrite_of_the_same_size_is_caught_by_the_first_bytes(self):
        """Neither the size nor the inode moves when a file is rewritten in place with something
        the same length; the fingerprint of its first bytes does."""
        old = self.path.read_text(encoding="utf-8")
        new = old.replace("first ask", "third ask").replace("one answer", "two answer")
        self.assertEqual(len(old), len(new), "the fixture rewrite has to be the same length")
        self.path.write_text(new, encoding="utf-8")
        os.utime(self.path, None)
        guest_sessions.reconcile(self.index, str(self.home))
        texts = [entry["text"] for entry in self.index.conversation(CLAUDE_ID)["items"]]
        self.assertIn("third ask", texts)
        self.assertNotIn("first ask", texts)

    def test_a_replaced_file_under_the_same_name_is_read_whole(self):
        other = self.root / "other.jsonl"
        other.write_text("".join(json.dumps(line) + "\n" for line in
                                 claude_lines(prompts=("a brand new session",), replies=(),
                                              custom_title="replaced")), encoding="utf-8")
        other.replace(self.path)                     # a different inode under the same name
        os.utime(self.path, None)
        guest_sessions.reconcile(self.index, str(self.home))
        self.assertEqual(["a brand new session"],
                         [entry["text"] for entry in self.index.conversation(CLAUDE_ID)["items"]])

    def test_a_cursor_without_its_row_reads_the_file_whole_next_time(self):
        """The row and the cursor are written in one transaction, so they cannot drift — but if
        they ever did, half a session is the one thing that must not be indexed."""
        self.append([claude_prompt_line("second ask", index=1)])
        parsed = guest_sessions.parse_claude_transcript(
            self.path, state=self.index.guest_cursors()[CLAUDE_ID])
        self.assertTrue(parsed["append"])
        self.index.delete_session(CLAUDE_ID, remove_files=False, forget=False)
        self.assertEqual(0, self.index.update_guest(parsed))
        self.assertEqual({}, self.index.guest_cursors())
        self.assertEqual(1, guest_sessions.reconcile(self.index, str(self.home))["added"])
        self.assertEqual(["prompt", "reply", "tool_call", "prompt"],
                         [entry["kind"] for entry in self.index.conversation(CLAUDE_ID)["items"]])


# ----- naming the live session (GT7X review, B7) ---------------------------------------------------

class NamedLiveSessions(unittest.TestCase):
    """Two guests in one working directory write two transcripts side by side. "The newest file"
    is then whichever of them typed last, so each pane tailed the other's session."""

    OTHER = "b" * 8 + "-0000-4000-8000-00000000000b"

    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.home = self.root / "home"
        self.home.mkdir()

    def two_claudes(self):
        mine = write_claude(self.home, claude_lines(prompts=("mine",), custom_title="mine"))
        theirs = write_claude(self.home, claude_lines(prompts=("theirs",), custom_title="theirs",
                                                      session_id=self.OTHER), session_id=self.OTHER)
        os.utime(mine, (1000, 1000))
        os.utime(theirs, (2000, 2000))
        return mine, theirs

    def test_a_named_claude_session_is_its_own_file_however_new_the_other_is(self):
        mine, theirs = self.two_claudes()
        self.assertEqual(theirs, guest_sessions.claude_live_transcript(CWD, str(self.home)))
        self.assertEqual(mine, guest_sessions.claude_live_transcript(CWD, str(self.home),
                                                                     session_id=CLAUDE_ID))
        self.assertEqual(mine, guest_sessions.live_transcript("claude", CWD, str(self.home),
                                                              session_id=CLAUDE_ID))
        # Without the workspace the id is still enough: the file is named after the session.
        self.assertEqual(mine, guest_sessions.claude_live_transcript(None, str(self.home),
                                                                     session_id=CLAUDE_ID))
        # An id with no transcript is None, never "the newest one instead".
        self.assertIsNone(guest_sessions.claude_live_transcript(CWD, str(self.home),
                                                                session_id="9" * 8))

    def test_a_named_codex_thread_is_its_own_rollout(self):
        older = write_codex(self.home, codex_lines(prompt="mine"))
        other = "7" * 8 + "-0000-4000-8000-000000000007"
        newer = write_codex(self.home, codex_lines(prompt="theirs", session_id=other),
                            name=f"rollout-2026-09-19T01-00-00-{other}.jsonl")
        os.utime(older, (1000, 1000))
        os.utime(newer, (2000, 2000))
        write_state_db(self.home, [{"id": CODEX_ID, "rollout_path": str(older), "cwd": CWD},
                                   {"id": other, "rollout_path": str(newer), "cwd": CWD}])
        self.assertEqual(newer, guest_sessions.codex_live_rollout(CWD, str(self.home)))
        self.assertEqual(older, guest_sessions.codex_live_rollout(CWD, str(self.home),
                                                                  thread_id=CODEX_ID))
        self.assertEqual(older, guest_sessions.live_transcript("codex", CWD, str(self.home),
                                                               session_id=CODEX_ID))
        self.assertIsNone(guest_sessions.codex_live_rollout(CWD, str(self.home), thread_id="nobody"))

    def test_a_named_codex_thread_is_found_without_the_database(self):
        """The rollout's name ends in the thread id, so `codex resume <id>` and this agree even
        when the threads database is missing or too old to have the row."""
        path = write_codex(self.home, codex_lines())
        self.assertEqual(CODEX_ID, guest_sessions.rollout_thread_id(path))
        self.assertEqual("", guest_sessions.rollout_thread_id(f"{CLAUDE_ID}.jsonl"))
        self.assertEqual(path, guest_sessions.codex_live_rollout(CWD, str(self.home),
                                                                 thread_id=CODEX_ID))

    def test_a_tail_can_be_built_from_the_session_id(self):
        mine, _theirs = self.two_claudes()
        tail = guest_sessions.LiveTail.for_session("claude", CWD, session_id=CLAUDE_ID,
                                                   home=str(self.home))
        self.assertEqual(mine, tail.path)
        self.assertEqual("mine", tail.record()["title"])
        self.assertIsNone(guest_sessions.LiveTail.for_session("claude", CWD, session_id="nobody",
                                                              home=str(self.home)))


class GuestTailTests(unittest.TestCase):
    """The worker's live session: the tail the pane follows, written into the index as it grows."""

    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.home = self.root / "home"
        self.home.mkdir()
        self.path = write_claude(self.home, claude_lines(prompts=("first ask",), replies=(),
                                                         custom_title="live one"))
        self.index = index_in(self.root)
        self.addCleanup(self.index.close)
        self.tail = guest_sessions.GuestTail(str(self.home), min_poll=0)
        self.addCleanup(self.tail.stop)

    def append(self, lines) -> None:
        with open(self.path, "a", encoding="utf-8") as handle:
            for line in lines:
                handle.write(json.dumps(line) + "\n")

    def test_the_live_session_is_in_the_index_before_any_reconcile(self):
        self.assertTrue(self.tail.start(self.index, "claude", CWD, CLAUDE_ID))
        self.assertEqual(self.path, self.tail.path)
        record = {row["id"]: row for row in guest_sessions.list_sessions(self.index)}[CLAUDE_ID]
        self.assertEqual(("live one", 1), (record["title"], record["message_count"]))
        self.assertEqual(CWD, record["resume_cwd"])

    def test_poll_says_when_there_is_something_new(self):
        self.tail.start(self.index, "claude", CWD, CLAUDE_ID)
        self.assertFalse(self.tail.poll(), "nothing appended, nothing to redraw")
        self.append([claude_reply_line("an answer", tool_use=False)])
        self.assertTrue(self.tail.poll())
        self.assertEqual(2, guest_sessions.list_sessions(self.index)[0]["message_count"])
        self.assertFalse(self.tail.poll())

    def test_poll_appends_rather_than_rewriting_the_session(self):
        self.tail.start(self.index, "claude", CWD, CLAUDE_ID)
        db = sqlite3.connect(str(self.root / "index.db"))
        self.addCleanup(db.close)
        before = [row[0] for row in db.execute("SELECT id FROM entries WHERE session_id=? AND kind='prompt'",
                                               (CLAUDE_ID,)).fetchall()]
        self.append([claude_reply_line("an answer", tool_use=False)])
        self.assertTrue(self.tail.poll())
        after = [row[0] for row in db.execute("SELECT id FROM entries WHERE session_id=? AND kind='prompt'",
                                              (CLAUDE_ID,)).fetchall()]
        self.assertEqual(before, after, "the prompt already indexed was not written again")
        self.assertEqual(["prompt", "reply"],
                         [entry["kind"] for entry in self.index.conversation(CLAUDE_ID)["items"]])

    def test_a_guest_that_has_not_written_yet_is_not_followed(self):
        self.assertFalse(self.tail.start(self.index, "claude", "/tmp/nowhere", CLAUDE_ID))
        self.assertFalse(self.tail.poll())
        self.assertIsNone(self.tail.path)
        self.assertIsNone(self.tail.record)
        with self.assertRaises(ValueError):
            self.tail.start(self.index, "gemini", CWD, CLAUDE_ID)

    def test_a_forgotten_session_is_not_written_back_by_the_tail(self):
        """The pane may well be running a session the user deleted from the list; following it
        is fine, putting it back in the index behind their back is not."""
        self.index.forget("claude", CLAUDE_ID)
        self.assertTrue(self.tail.start(self.index, "claude", CWD, CLAUDE_ID))
        self.append([claude_reply_line("an answer", tool_use=False)])
        self.tail.poll()
        self.assertEqual([], guest_sessions.list_sessions(self.index))

    def test_the_tail_never_rebuilds_a_row_from_the_middle(self):
        """If the row goes while the pane is running, the tail holds only the entries since its
        last write: half a session in the list is worse than none, and the next reconcile reads
        the transcript whole."""
        self.tail.start(self.index, "claude", CWD, CLAUDE_ID)
        self.index.delete_session(CLAUDE_ID, remove_files=False, forget=False)
        self.append([claude_reply_line("an answer", tool_use=False)])
        self.assertFalse(self.tail.poll())
        self.assertEqual([], guest_sessions.list_sessions(self.index))
        self.assertEqual(1, guest_sessions.reconcile(self.index, str(self.home))["added"])
        self.assertEqual(["prompt", "reply"],
                         [entry["kind"] for entry in self.index.conversation(CLAUDE_ID)["items"]])

    def test_stop_leaves_what_it_wrote_and_polls_no_more(self):
        self.tail.start(self.index, "claude", CWD, CLAUDE_ID)
        self.tail.stop()
        self.append([claude_reply_line("after the pane closed", tool_use=False)])
        self.assertFalse(self.tail.poll())
        self.assertEqual(1, guest_sessions.list_sessions(self.index)[0]["message_count"])


# ----- the fixtures are the real formats ------------------------------------------------------------

class FixtureFormat(unittest.TestCase):
    """`tests/fixtures/guest/` holds one transcript of each guest in its real shape; parsing it
    here fails if the guest's format (or the parser) drifts."""

    def test_claude_fixture_parses(self):
        path = FIXTURES / f"claude-{CLAUDE_ID}.jsonl"
        parsed = guest_sessions.parse_claude_transcript(path)
        self.assertEqual(("claude", CLAUDE_ID, CWD), (parsed["source"], parsed["id"], parsed["workspace"]))
        self.assertEqual("pane-drag-fix", parsed["title"])
        self.assertEqual(4, parsed["message_count"])
        self.assertEqual(["prompt", "reply", "tool_call", "prompt", "reply", "tool_call"],
                         [row["kind"] for row in parsed["entries"]])

    def test_codex_fixture_parses(self):
        path = FIXTURES / CODEX_NAME
        parsed = guest_sessions.parse_codex_rollout(path)
        self.assertEqual(("codex", CODEX_ID, CWD), (parsed["source"], parsed["id"], parsed["workspace"]))
        self.assertEqual("what does the diff view do", parsed["title"])
        self.assertEqual(2, parsed["message_count"])

    def test_codex_state_fixture_reads(self):
        meta = guest_sessions.codex_thread_meta(FIXTURES / "state_5.sqlite")
        self.assertEqual("Diff view", meta[CODEX_ID]["name"])
        self.assertEqual(CWD, meta[CODEX_ID]["workspace"])


if __name__ == "__main__":
    unittest.main()
