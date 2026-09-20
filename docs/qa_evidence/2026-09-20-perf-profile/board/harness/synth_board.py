#!/usr/bin/env python3
"""Grow a copy of Relay's own board to N cards by cloning real cards with fresh ids.

    python3 synth_board.py <src board dir> <dst board dir> <N>

Every clone keeps its source card's body, labels and status distribution, gets a fresh
4-char id and a fresh rank, and — when the source had a thread — a cloned thread under the
new id.  `relay-board.py check` validates the result.
"""
import random
import re
import shutil
import string
import sys
from pathlib import Path

sys.path.insert(0, str(Path('/tmp/claude-1000/pf4k/src/backend')))
from relay_core import board as B  # noqa: E402

ALPHABET = string.ascii_uppercase + string.digits


def main() -> int:
    src, dst, want = Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3])
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    board = B.Board(dst)
    originals = board.card_paths()
    taken = set()
    for p in originals:
        c = B.Card.load(p)
        if c.id:
            taken.add(c.id)
    rng = random.Random(20260920)
    ranks = B.initial_ranks(want + len(originals))
    made = 0
    n = 0
    while len(originals) + made < want:
        proto = originals[n % len(originals)]
        n += 1
        text = proto.read_text(encoding='utf-8')
        card = B.Card.parse(text, proto)
        old_id = card.id
        while True:
            new_id = ''.join(rng.choice(ALPHABET) for _ in range(4))
            if B.valid_id(new_id) and new_id not in taken:
                break
        taken.add(new_id)
        card.set('id', new_id)
        card.set('rank', ranks[len(originals) + made])
        # a unique title so the index rows differ
        card.body = re.sub(r'^# (.*)$', lambda m: f"# {m.group(1)} ({new_id})",
                           card.body, count=1, flags=re.M)
        folder = proto.parent
        out = folder / f"{proto.stem}-{new_id.lower()}.md"
        out.write_text(card.to_text(), encoding='utf-8')
        # clone the thread too, when the prototype had one
        if old_id:
            th = board.thread_path(old_id)
            if th.exists():
                (board.threads_dir() / f"{new_id}.md").write_text(
                    th.read_text(encoding='utf-8'), encoding='utf-8')
        made += 1
    print(f"{len(originals)} originals + {made} clones = {len(B.Board(dst).card_paths())} cards")
    return 0


if __name__ == '__main__':
    sys.exit(main())
