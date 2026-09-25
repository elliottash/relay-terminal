# SPDX-License-Identifier: AGPL-3.0-or-later
"""The Board's link index (#EE42, docs/PROJECT-BOARD-DESIGN.md §4, protocol 19.24).

`board_links` computes both directions of every link from the files; the mentioned-in line is
the one stored reverse; `duplicate_of`, `discovered_from` and `supersedes` are work-card links;
`dangling_link` names an address that resolves to nothing; `board_search` follows link terms.
Every test works in a temporary board.
"""
import json
import subprocess
import unittest

from relay_core import board as B
from relay_core import board_links as L
from relay_core import cases as CASES
from tests import test_board_protocol as TBP
from tests import test_board_tools as TBT


class AddressTests(unittest.TestCase):
    def test_mentions_reads_every_address_form_once(self):
        text = ("see #K7Q2 and #K7Q2 again, skill:referee-review@sha256:abcdef0123456789, "
                "case:c-8f3a2b1c, run:r-3e9d1a20#figures/fig2.pdf and "
                "src/BoardPane.cpp:3901@e5a0bc03.")
        found = dict(L.mentions(text))
        self.assertEqual(list(found), ["#K7Q2", "skill:referee-review", "case:c-8f3a2b1c",
                                       "run:r-3e9d1a20", "src/BoardPane.cpp@e5a0bc03"])
        self.assertEqual(found["skill:referee-review"], {"version": "sha256:abcdef0123456789"})
        self.assertEqual(found["run:r-3e9d1a20"], {"artifact": "figures/fig2.pdf"})
        self.assertEqual(found["src/BoardPane.cpp@e5a0bc03"], {"line": 3901})

    def test_code_colours_anchors_and_digits_are_not_links(self):
        text = ("`#K7Q2` in code, ```\n#M2P3\n``` fenced, colour #FFAA00, anchor (#plan), "
                "url https://x.org/a#K7Q2, digits #1234, word a#K7Q2, mail e@elliottash.com")
        self.assertEqual(L.mentions(text), [])

    def test_normalize(self):
        self.assertEqual(L.normalize("#k7q2"), "#K7Q2")
        self.assertEqual(L.normalize("K7Q2"), "#K7Q2")
        self.assertEqual(L.normalize("links:#K7Q2"), "#K7Q2")
        self.assertEqual(L.normalize("skill:deliver@sha256:abcdef01"), "skill:deliver")
        self.assertEqual(L.normalize("run:r-3e9d1a20#fig.pdf"), "run:r-3e9d1a20")
        self.assertEqual(L.normalize("src/a.cpp:12@e5a0bc03"), "src/a.cpp@e5a0bc03")
        self.assertEqual(L.normalize("E5A0BC03"), "e5a0bc03")
        self.assertIsNone(L.normalize("two words"))
        self.assertEqual(L.kind_of("docs/x.md"), "file")


class LinkToolsTest(TBT.BoardToolsTest):
    """A temp board with the agent's tools (see test_board_tools.BoardToolsTest)."""

    def read(self, card_id):
        result = self.tools.run("board_read", {"id": card_id})
        self.assertNotIn("error", result, result)
        return result

    def update(self, card_id, **args):
        return self.tools.run("board_update_card",
                              {"id": card_id, "base_hash": self.read(card_id)["hash"], **args})

    def comment(self, card_id, text):
        result = self.tools.run("board_comment", {"id": card_id, "text": text, "kind": "note"})
        self.assertNotIn("error", result, result)

    def mention_entries(self, card_id):
        return [e for e in self.board.thread(card_id) if e.attrs.get("mention")]

    def index(self):
        return L.build(self.board)


class IndexTests(LinkToolsTest):
    def test_forward_and_reverse_typed_by_field(self):
        a = self.create(title="Alpha")
        b = self.create(title="Beta child", request="build the beta child part", parent=a,
                        not_duplicate_of=[a])
        c = self.create(title="Gamma blocked", request="gamma waits for alpha", blocked_by=[a],
                        not_duplicate_of=[a, b])
        d = self.create(title="Delta mentions", request=f"relates to #{a} somehow",
                        not_duplicate_of=[a, b, c])
        ix = self.index()
        reverse = {(r["from"], r["name"], r["relation"]) for r in ix.query("#" + a)["reverse"]}
        self.assertIn(("#" + b, "children", "parent"), reverse)
        self.assertIn(("#" + c, "blocks", "blocked_by"), reverse)
        self.assertIn(("#" + d, "mentioned_in", "mention"), reverse)
        forward = ix.query("#" + b)["forward"]
        self.assertEqual([(f["relation"], f["to"], f["where"], f["exists"]) for f in forward],
                         [("parent", "#" + a, "front matter", True)])
        self.assertEqual(ix.matching(f"links:#{a}"), {b, c, d})

    def test_board_read_reverse_and_links_index_share_one_table(self):
        a = self.create(title="Alpha")
        b = self.create(title="Beta", request="beta request text", not_duplicate_of=[a])
        self.assertIn("discovered_from", self.update(b, fields={"discovered_from": a,
                                                                "supersedes": [a]})["changes"][0])
        reverse = self.read(a)["reverse"]
        self.assertEqual([r["id"] for r in reverse["discovered"]], [b])
        self.assertEqual([r["id"] for r in reverse["superseded_by"]], [b])
        names = {r["name"] for r in self.index().query("#" + a)["reverse"]}
        self.assertLessEqual({"discovered", "superseded_by"}, names)

    def test_ledger_rows_link_cases_to_cards_and_skills(self):
        a = self.create(title="Alpha")
        row = {"id": "c-8f3a2b1c", "when": "2026-09-25T10:00:00Z", "server": "referee-report",
               "card": a, "served_by": "owner"}
        CASES.ledger_path(self.root).write_text(json.dumps(row) + "\n", encoding="utf-8")
        ix = self.index()
        self.assertEqual([(r["from"], r["name"]) for r in ix.query("#" + a)["reverse"]],
                         [("case:c-8f3a2b1c", "cases")])
        self.assertTrue(ix.query("case:c-8f3a2b1c")["exists"])
        self.assertTrue(ix.exists("skill:referee-report"))       # the ledger knows the server
        self.assertEqual(ix.matching("skill:referee-report"), {a})
        self.assertEqual(ix.matching("case:c-8f3a2b1c"), {a})
        self.assertIsNone(ix.matching("plain"))

    def test_index_is_a_function_of_the_files(self):
        a = self.create(title="Alpha")
        b = self.create(title="Beta", request=f"see #{a}", not_duplicate_of=[a])
        self.assertTrue(self.index().reverse.get("#" + a))
        card = self.board.card_by_id(b)
        card.body = card.body.replace(f"#{a}", "nothing")
        card.dirty = True
        self.board.save(card)
        self.assertFalse([e for e in self.index().reverse.get("#" + a, [])
                          if e["from"] == "#" + b and e["where"] == "body"])


class FieldTests(LinkToolsTest):
    def test_new_fields_must_name_cards_on_this_board(self):
        a = self.create(title="Alpha")
        for field, value in (("discovered_from", "ZZZZ"), ("supersedes", ["ZZZZ"])):
            result = self.update(a, fields={field: value})
            self.assertEqual(result.get("code"), "board_refused", result)
            self.assertIn("not a card on this board", result["error"])
        self.assertEqual(self.update(a, fields={"discovered_from": a}).get("code"), "board_refused")

    def test_memory_supersedes_is_left_as_written(self):
        result = self.tools.run("board_create_card", {
            "type": "memory", "status": "active", "title": "Tabs", "summary": "tabs are two spaces"})
        self.assertNotIn("error", result, result)
        self.assertNotIn("error", self.update(result["id"], fields={"supersedes": ["old-name"]}))
        self.assertEqual(self.board.card_by_id(result["id"]).front["supersedes"], ["old-name"])

    def test_duplicate_of_through_update_closes_the_card(self):
        a = self.create(title="Alpha")
        b = self.create(title="Beta", request="beta words", not_duplicate_of=[a])
        result = self.update(b, fields={"duplicate_of": a})
        self.assertNotIn("error", result, result)
        self.assertEqual(result["closed"]["status"], "dropped")
        card = self.board.card_by_id(b)
        self.assertEqual((card.status, card.front.get("resolution"), card.front.get("duplicate_of")),
                         ("dropped", "duplicate", a))
        self.assertIn("/done/", str(card.path))
        self.assertEqual(result["hash"], B.file_hash(card.path))
        self.assertEqual([r["id"] for r in self.read(a)["reverse"]["duplicated_by"]], [b])

    def test_discovered_from_is_the_card_this_pane_holds(self):
        held = self.create(title="Held work")
        self.assertNotIn("error", self.tools.run("board_claim", {"id": held}))
        found = self.create(title="A fault found on the way", request="the fault",
                            not_duplicate_of=[held])
        self.assertEqual(self.board.card_by_id(found).front.get("discovered_from"), held)
        other = self.create(title="Explicit origin", request="named origin",
                            not_duplicate_of=[held, found], discovered_from=found)
        self.assertEqual(self.board.card_by_id(other).front.get("discovered_from"), found)
        refused = self.tools.run("board_create_card", {
            "tab": "features", "status": "inbox", "title": "Bad origin", "request": "x",
            "discovered_from": "ZZZZ"})
        self.assertEqual(refused.get("code"), "board_refused", refused)

    def test_check_warns_dangling_link_on_the_new_fields(self):
        a = self.create(title="Alpha")
        card = self.board.card_by_id(a)
        card.set("supersedes", ["ZZZZ"])
        card.set("discovered_from", "YYYY")
        self.board.save(card)
        found = [p.message for p in self.board.check() if p.code == "dangling_link"]
        self.assertEqual(len(found), 2, found)
        self.assertTrue(any("supersedes names ZZZZ" in m for m in found))


class MentionLineTests(LinkToolsTest):
    def test_a_new_reference_leaves_one_line_on_the_target(self):
        a = self.create(title="Alpha")
        b = self.create(title="Beta", request="beta words", not_duplicate_of=[a])
        self.comment(b, f"this depends on #{a}")
        self.comment(b, f"still about #{a}")
        self.update(b, fields={"links": {"related": [a]}})
        entries = self.mention_entries(a)
        self.assertEqual(len(entries), 1, self.thread_text(a))
        entry = entries[0]
        self.assertEqual((entry.kind, entry.attrs["mention"], entry.author), ("event", b, "agent"))
        self.assertRegex(entry.text, rf"^mentioned in #{b} · \d{{4}}-\d\d-\d\d · agent$")
        self.assertEqual(self.mention_entries(b), [])        # the line is not itself a link

    def test_create_with_a_mention_notes_it_and_existing_refs_do_not(self):
        a = self.create(title="Alpha")
        b = self.create(title="Beta", request=f"follows #{a}", not_duplicate_of=[a])
        self.assertEqual([e.attrs["mention"] for e in self.mention_entries(a)], [b])
        self.update(b, append_section={"heading": "Plan", "text": "no new reference"})
        self.assertEqual(len(self.mention_entries(a)), 1)

    def test_unknown_targets_and_self_get_nothing_and_writes_never_fail(self):
        a = self.create(title="Alpha")
        self.comment(a, f"see #{a} and #ZZZZ")
        self.assertEqual(self.mention_entries(a), [])
        self.assertFalse(self.board.thread_path("ZZZZ").exists())

    def test_note_mentions_is_idempotent_per_pair(self):
        a = self.create(title="Alpha")
        b = self.create(title="Beta", request="beta words", not_duplicate_of=[a])
        source = self.board.card_by_id(b)
        before = self.thread_text(a)
        self.assertEqual(L.note_mentions(self.board, source, [a], "codex"), [a])
        self.assertEqual(L.note_mentions(self.board, source, [a, b, "ZZZZ"], "codex"), [])
        after = self.thread_text(a)
        self.assertEqual(after.count("mention="), 1)
        self.assertTrue(after.startswith(before))
        ids = [e.entry_id for e in B.parse_thread(after)]
        self.assertEqual(ids, sorted(ids))


class DanglingTests(LinkToolsTest):
    def test_prose_and_git_addresses_that_name_nothing(self):
        subprocess.run(["git", "init", "-q", str(self.repo)], check=True)
        (self.repo / "a.txt").write_text("a\n", encoding="utf-8")
        subprocess.run(["git", "-C", str(self.repo), "add", "a.txt"], check=True)
        subprocess.run(["git", "-C", str(self.repo), "-c", "user.name=t", "-c", "user.email=t@t",
                        "commit", "-q", "-m", "a"], check=True)
        sha = subprocess.run(["git", "-C", str(self.repo), "rev-parse", "--short=8", "HEAD"],
                             capture_output=True, text=True, check=True).stdout.strip()
        a = self.create(title="Alpha", request=(f"see #ZZZZ, case:c-deadbeef, a.txt@{sha}, "
                                                f"b.txt@{sha} and run:r-00ff00ff"))
        ix = self.index()
        dangling = {(e["from"], e["to"]) for e in ix.dangling()}
        self.assertEqual(dangling, {("#" + a, "#ZZZZ"), ("#" + a, "case:c-deadbeef"),
                                    ("#" + a, f"b.txt@{sha}")})
        self.assertTrue(ix.exists(f"a.txt@{sha}"))
        self.assertIsNone(ix.exists("run:r-00ff00ff"))       # no runs ledger yet (#FVVY)


class ProtocolLinkTests(TBP.ProtocolTest):
    def test_board_links_answers_per_address_and_dangling(self):
        a = self.make_card(title="Alpha")
        b = self.make_card(title="Beta", text=f"builds on #{a} and #ZZZZ", not_duplicate_of=[a])
        events = self.send(type="board_links", id="l1", address=f"#{a.lower()}")
        answer = self.of("board_links")[0]
        self.assertEqual(answer["id"], "l1")
        self.assertEqual(answer["items"][0]["address"], "#" + a)
        self.assertEqual([(r["from"], r["name"]) for r in answer["items"][0]["reverse"]],
                         [("#" + b, "mentioned_in")])
        self.assertNotIn("dangling", answer)
        self.send(type="board_links", id="l2")
        dangling = self.of("board_links")[0]["dangling"]
        self.assertEqual([(d["from"], d["to"]) for d in dangling], [("#" + b, "#ZZZZ")])
        with self.assertRaises(ValueError):
            self.commands.dispatch({"type": "board_links", "id": "l3", "address": "two words"})
        self.assertTrue(events)
        # The owner's create through the pane left the mentioned-in line too.
        self.assertEqual([e.attrs.get("mention") for e in self.board.thread(a)
                          if e.attrs.get("mention")], [b])

    def test_board_search_follows_link_terms(self):
        a = self.make_card(title="Alpha")
        b = self.make_card(title="Beta voice", text=f"relates to #{a}", not_duplicate_of=[a])
        c = self.make_card(title="Gamma", text=f"also #{a}", not_duplicate_of=[a, b])
        self.send(type="board_refresh")
        self.send(type="board_search", id="s1", query=f"links:#{a}")
        self.assertEqual(self.of("board_search")[0]["ids"], sorted([b, c]))
        self.send(type="board_search", id="s2", query=f"links:#{a} voice")
        self.assertEqual(self.of("board_search")[0]["ids"], [b])
        self.send(type="board_search", id="s3", query="skill:nothing-here")
        self.assertEqual(self.of("board_search")[0]["ids"], [])


if __name__ == "__main__":
    unittest.main()
