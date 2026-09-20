#!/usr/bin/env python3
"""One write a second through Relay's own board writers: a card save, a thread append, the index.

    storm_relay.py <backend source root> <workspace> <card id> <writes>
"""
import sys, time, os
sys.path.insert(0, sys.argv[1] + '/backend')
from relay_core import board as B          # noqa: E402

ws, card_id, n = sys.argv[2], sys.argv[3], int(sys.argv[4])
board = B.Board(B.board_folder(ws), ws)
for i in range(n):
    if i % 3 == 0:
        card = board.card_by_id(card_id)
        card.set('assignee', f'agent{i % 2}')
        board.save(card)
    elif i % 3 == 1:
        board.append_thread(card_id, f'storm {i}', author='agent', kind='progress')
    else:
        B._atomic_write(board.root / 'BOARD.md',
                        (board.root / 'BOARD.md').read_text(encoding='utf-8') + f'\n<!-- {i} -->\n')
    time.sleep(1)
