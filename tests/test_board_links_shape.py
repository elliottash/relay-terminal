"""A card whose `links` is not a mapping must not break the Board's read paths (2026-09-25:
one hand-edited card with `links: [ids]` made every board_refresh fail with AttributeError)."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))

from relay_core import board as B  # noqa: E402


def card(text: str) -> B.Card:
    return B.Card.parse(text)


class LinksShape(unittest.TestCase):
    def test_list_links_reads_as_no_links(self):
        bad = card("---\nid: AAAA\nstatus: todo\nlinks:\n  - BBBB\n---\n# Bad\n")
        good = card("---\nid: CCCC\nstatus: todo\nlinks:\n  related: [BBBB]\n---\n# Good\n")
        self.assertEqual(B.card_links(bad), {})
        index = B.links_index([bad, good])
        self.assertEqual([row["id"] for row in index["related_from"]["BBBB"]], ["CCCC"])


if __name__ == "__main__":
    unittest.main()
