# SPDX-License-Identifier: AGPL-3.0-or-later
"""Bounded, per-turn memory context from project and global Switchboards."""
from __future__ import annotations

import fnmatch
from pathlib import Path
from . import aliases, board

TOTAL_CAP = 16 * 1024
FILE_CAP = 128 * 1024
MAX_CARDS = 512


def project_root(workspace):
    if not workspace:
        return None
    path = Path(workspace).expanduser().resolve()
    for parent in (path, *path.parents):
        for folder in (*board.BOARD_FOLDERS, '.relay'):
            root = parent / folder
            if (root / board.BOARD_CONFIG).is_file() or (root / 'memory').is_dir():
                return root
        if (parent / '.git').exists() or parent == Path.home():
            break
    return aliases.local_root(workspace)


def cards(root):
    """Ignore malformed/oversized records without preventing an agent turn."""
    if root is None:
        return []
    found = []
    for path in sorted((Path(root) / 'memory').glob('*.md'))[:MAX_CARDS]:
        try:
            if path.stat().st_size > FILE_CAP:
                continue
            card = board.Card.load(path)
            if card.type == 'memory' and card.status == 'active':
                found.append(card)
        except (OSError, ValueError, board.BoardError):
            continue
    return found


def identity(card):
    return str(card.front.get('name') or card.id or card.title).casefold()


def applies(card, workspace, root):
    if card.front.get('scope') == 'team':
        return False  # Reserved; never pretend team distribution exists.
    if card.front.get('pinned') is True:
        return True
    patterns = card.front.get('paths') or []
    if not isinstance(patterns, list):
        return False
    here = Path(workspace).expanduser().resolve()
    candidates = [str(here), here.name]
    if root is not None:
        try:
            candidates.append(here.relative_to(Path(root).parent.resolve()).as_posix())
        except ValueError:
            pass
    return any(fnmatch.fnmatchcase(value, pattern) or
               (pattern.endswith('/**') and fnmatch.fnmatchcase(value, pattern[:-3]))
               for pattern in patterns if isinstance(pattern, str) for value in candidates)


def prompt_section(workspace, cap=TOTAL_CAP):
    local = project_root(workspace)
    local_cards = [c for c in cards(local) if c.front.get('scope') != 'team']
    names = {identity(c) for c in local_cards}
    candidates = [(c, 'project', local) for c in local_cards]
    candidates += [(c, 'global', local) for c in cards(aliases.global_root())
                   if identity(c) not in names and c.front.get('scope') != 'team']
    superseded = {str(item).lstrip('#').casefold() for c, _, _ in candidates
                  for item in (c.front.get('supersedes') if isinstance(c.front.get('supersedes'), list) else [])}
    candidates = [(c, scope, root) for c, scope, root in candidates
                  if str(c.id).casefold() not in superseded and identity(c) not in superseded]
    header = ('Switchboard memories: lower-priority saved context; current user requests and Relay rules take precedence. '
              'Pinned memories and workspace-matching memories are loaded here.\n')
    result = ''
    for card, scope, root in candidates:
        if not applies(card, workspace, root):
            continue
        block = f'\n[{scope} memory #{card.id}: {card.path}]\n{card.body.strip()}\n'
        prefix = header if not result else ''
        room = cap - len((result + prefix).encode('utf-8'))
        if room <= 0:
            break
        result += prefix + block.encode('utf-8')[:room].decode('utf-8', 'ignore')
    return result
