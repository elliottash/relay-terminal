"""Execute (board_claim) on a card with and without `## Done means`, through the wire."""
import sys, pathlib, unittest
X = pathlib.Path(sys.argv[1]); sys.argv = sys.argv[:1]
sys.path[:0] = [str(X / "backend"), str(X / "tests")]
from test_board_protocol import ProtocolTest
from relay_core import board as B

PANE = "3f2504e0-4f89-11d3-9a0c-0305e82c3301"

class Drive(ProtocolTest):
    def section(self, cid, heading, text):
        card = self.board.card_by_id(cid)
        self.send(type="board_update", card=cid, base_hash=B.file_hash(card.path),
                  patch={"append_section": {"heading": heading, "text": text}})

    def test_drive(self):
        cid = self.make_card()
        ev = self.send(type="board_claim", id="x1", card=cid, pane_token=PANE)
        w = [e for e in ev if e["event"] == "board_written"]
        err = [e for e in ev if e["event"] == "error"]
        print("NO Done means -> notice:", repr(w[0].get("notice")))
        print("                errors:", err, " status:", self.board.card_by_id(cid).status)

        cid2 = self.make_card(title="Has expectations")
        self.section(cid2, "Done means",
                     "- the key records and inserts the transcript at the caret\n"
                     "Failure would show as: silence, or a stuck indicator.")
        ev = self.send(type="board_claim", id="x2", card=cid2, pane_token=PANE)
        w = [e for e in ev if e["event"] == "board_written"]
        print("WITH Done means -> notice key present:", "notice" in w[0],
              " status:", self.board.card_by_id(cid2).status)

        # and the section written in the same breath as the claim does not warn afterwards
        cid3 = self.make_card(title="Gains it later")
        self.section(cid3, "Done means", "- x\nFailure: y")
        ev = self.send(type="board_claim", id="x3", card=cid3, pane_token=PANE)
        w = [e for e in ev if e["event"] == "board_written"]
        print("gained-it-first    -> notice key present:", "notice" in w[0])

unittest.main(argv=["x", "-q"], exit=False)
