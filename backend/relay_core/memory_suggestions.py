# SPDX-License-Identifier: AGPL-3.0-or-later
"""Learned user memory as suggestions the user confirms, with remembered rejections (#MEMS).

Owner, 2026-09-22: "new memories are suggestions that the user confirms. and if the user rejects,
rejections are remembered so that, new potentialy memories will be declined if they trigger that
same suggestion again."

Both kinds are memory cards in the global Board, one level below the active ones::

    <global root>/memory/suggestions/<ID>.md   status: suggested  (waiting for Keep / Edit / No)
    <global root>/memory/rejected/<ID>.md      status: rejected   (the user said No; kept)

`memories.cards()` reads only `memory/*.md` and only `status: active`, so neither folder ever
reaches an agent's context, and `GlobalsCommands._records` leaves both out of the editor list.
Keep is `accept()`: it writes the ordinary active user-memory card through the Globals save path,
so it gets the same validation, name check and history as an edit in Globals > User memory.

A new suggestion is compared with every rejection, every active user memory and every pending
suggestion (`_matches`): the same explicit name, the same normalised text, or a word-set overlap
of at least `SIMILAR` after dropping `STOPWORDS`. A match with a rejection is *declined* and a
match with the other two is a *duplicate*; neither writes a file.
"""
from __future__ import annotations

import os
import re
from datetime import datetime
from pathlib import Path

from . import aliases, board, memories
from . import filelock as fcntl

SUGGESTIONS = 'suggestions'
REJECTED = 'rejected'
MAX_FACT = 2000
MAX_TITLE = 120
SIMILAR = 0.8

#: Words that carry no meaning of their own in a one-sentence fact about a person, so that
#: "The user prefers tabs" and "User prefers tabs." are the same suggestion.
STOPWORDS = frozenset(
    'a an the and or but of to in on at for with by from as is are was were be been being '
    'it its this that these those they them their he she him his her i me my we our you your '
    'user users person s'.split())


class SuggestionError(ValueError):
    """A request this store refuses; the message is written for the person or agent."""


def normalise(text: str) -> str:
    """Casefolded, punctuation stripped (apostrophes dropped, the rest a space), one space."""
    text = str(text or '').casefold().replace("'", '').replace('’', '')
    return ' '.join(re.sub(r'[\W_]+', ' ', text).split())


def _words(text: str) -> frozenset:
    return frozenset(w for w in normalise(text).split() if w not in STOPWORDS)


def slug(text: str, limit: int = 60) -> str:
    """A kebab-case name from the first meaningful words of `text`."""
    words = [w for w in normalise(text).split() if w not in STOPWORDS] or normalise(text).split()
    out = ''
    for word in words:
        candidate = f'{out}-{word}' if out else word
        if len(candidate) > limit:
            break
        out = candidate
    return out or 'memory'


def _title_of(fact: str) -> str:
    first = re.split(r'(?<=[.!?])\s', fact.strip(), maxsplit=1)[0].strip().rstrip('.')
    if len(first) <= 72:
        return first
    return first[:72].rsplit(' ', 1)[0].rstrip(',;:') + '…'


def _today() -> str:
    return datetime.now().strftime('%Y-%m-%d')


def _memory_dir() -> Path:
    return aliases.global_root() / board.MEMORY_FOLDER


def _fact_of(card: board.Card) -> str:
    return '\n'.join(line for line in card.body.strip().splitlines()
                     if not line.startswith('# ')).strip()


def _record(card: board.Card, date_field: str) -> dict:
    return dict(id=card.id, name=str(card.front.get('name') or ''), title=card.title,
                fact=_fact_of(card), source=str(card.front.get('source') or ''),
                origin=str(card.front.get('origin') or ''),
                date=str(card.front.get(date_field) or card.front.get('created') or ''),
                path=str(card.path), **({'reason': str(card.front['reason'])}
                                        if card.front.get('reason') else {}))


def _load(folder: str, status: str) -> list[board.Card]:
    found = []
    for path in sorted((_memory_dir() / folder).glob('*.md'))[:memories.MAX_CARDS]:
        try:
            if path.stat().st_size > memories.FILE_CAP:
                continue
            card = board.Card.load(path)
            if card.type == 'memory' and card.status == status and card.id:
                found.append(card)
        except (OSError, ValueError, board.BoardError):
            continue
    return found


def _active_user_cards() -> list[board.Card]:
    return [c for c in memories.cards(aliases.global_root())
            if str(c.front.get('scope') or 'user') == 'user']


def _newest_first(records: list[dict]) -> list[dict]:
    return sorted(records, key=lambda r: (r['date'], r['id']), reverse=True)


def pending() -> list[dict]:
    """Suggestions waiting for the user, newest first."""
    return _newest_first([_record(c, 'suggested') for c in _load(SUGGESTIONS, 'suggested')])


def rejected() -> list[dict]:
    """Suggestions the user said No to, newest first. Never loaded into context."""
    return _newest_first([_record(c, 'rejected') for c in _load(REJECTED, 'rejected')])


def rejection_digest(limit: int = 40) -> str:
    """One short line per rejected fact, newest first — for an agent's tool result."""
    lines = []
    for row in rejected()[:max(0, int(limit))]:
        fact = ' '.join(row['fact'].split()) or row['title']
        if len(fact) > 160:
            fact = fact[:159].rstrip() + '…'
        reason = f" ({row['reason']})" if row.get('reason') else ''
        lines.append(f"- {fact} [rejected {row['date']}]{reason}")
    return '\n'.join(lines)


def _similar(name: str | None, fact: str, card: board.Card) -> bool:
    other_fact = _fact_of(card) or card.title
    if name and name.casefold() == str(card.front.get('name') or '').casefold():
        return True
    if normalise(fact) and normalise(fact) == normalise(other_fact):
        return True
    mine, theirs = _words(fact), _words(other_fact)
    return bool(mine and theirs) and len(mine & theirs) / len(mine | theirs) >= SIMILAR


def _matches(name: str | None, fact: str) -> dict | None:
    """The first stored card this suggestion repeats: rejections first, since those decline."""
    for kind, cards, date_field in (('rejected', _load(REJECTED, 'rejected'), 'rejected'),
                                    ('active', _active_user_cards(), 'reviewed'),
                                    ('pending', _load(SUGGESTIONS, 'suggested'), 'suggested')):
        for card in cards:
            if _similar(name, fact, card):
                found = dict(id=card.id, kind=kind, name=str(card.front.get('name') or ''),
                             fact=_fact_of(card) or card.title,
                             date=str(card.front.get(date_field) or card.front.get('created') or ''))
                if card.front.get('reason'):
                    found['reason'] = str(card.front['reason'])
                return found
    return None


def _clean_fact(fact) -> str:
    if not isinstance(fact, str) or not fact.strip():
        raise SuggestionError('A suggestion needs the fact to remember, as one or two sentences.')
    fact = fact.strip()
    if len(fact) > MAX_FACT or '\x00' in fact:
        raise SuggestionError(f'A suggested fact is plain text of at most {MAX_FACT} characters.')
    return fact


def _clean_title(title, fact: str) -> str:
    if title is not None and not isinstance(title, str):
        raise SuggestionError('A title is text.')
    title = ' '.join((title or '').split()) or _title_of(fact)
    return title[:MAX_TITLE]


class _Lock:
    """The suggestions folder's directory lock. Not `memory/` itself: `accept()` calls the
    Globals save path, which takes that one, while this is held."""

    def __enter__(self):
        folder = _memory_dir() / SUGGESTIONS
        folder.mkdir(parents=True, exist_ok=True)
        self.fd = fcntl.open_directory_lock(folder)
        fcntl.flock(self.fd, fcntl.LOCK_EX)
        return self

    def __exit__(self, *exc):
        os.close(self.fd)


def _taken_ids() -> set[str]:
    root = _memory_dir()
    return {p.stem.upper() for p in root.rglob('*.md')} if root.is_dir() else set()


def suggest(fact: str, *, name: str | None = None, title: str | None = None,
            source: str = 'agent', origin: str = '') -> dict:
    """Record a fact the user has not confirmed yet, unless it repeats a rejection or a memory.

    Returns `{status, id, name, fact, source, matched}`: status `pending` (a new file, `id` set),
    `declined` (it repeats a rejection) or `duplicate` (it repeats an active user memory or a
    pending suggestion); `matched` names the stored card for the last two, and `id` is None.
    A `name` given by the caller is also matched by name; a derived one is not, since it is only
    the fact's first words and would make two different facts collide.
    """
    fact = _clean_fact(fact)
    explicit = slug(name) if isinstance(name, str) and name.strip() else None
    name = explicit or slug(fact)
    title = _clean_title(title, fact)
    source = ' '.join(str(source or 'agent').split())[:80] or 'agent'
    origin = ' '.join(str(origin or '').split())[:400]
    with _Lock():
        matched = _matches(explicit, fact)
        if matched:
            status = 'declined' if matched['kind'] == 'rejected' else 'duplicate'
            return dict(status=status, id=None, name=name, fact=fact, source=source, matched=matched)
        root = aliases.global_root()
        if not (root / board.BOARD_CONFIG).exists():
            board.atomic_write(root / board.BOARD_CONFIG, board.CONFIG_TEXT)
        card_id = board.new_id(_taken_ids())
        card = board.Card(front=dict(id=card_id, type='memory', status='suggested', name=name,
                                     rank=board.initial_ranks(1)[0], created=_today(),
                                     scope='user', source=source, origin=origin,
                                     suggested=_today()),
                          body=f'# {title}\n\n{fact}\n', dirty=True)
        board.atomic_write(_memory_dir() / SUGGESTIONS / f'{card_id}.md', card.to_text())
    return dict(status='pending', id=card_id, name=name, fact=fact, source=source, matched=None)


def _pending_card(sid) -> board.Card:
    if not isinstance(sid, str) or not board.valid_id(sid.strip().lstrip('#').upper()):
        raise SuggestionError('Name the suggestion by its id.')
    path = _memory_dir() / SUGGESTIONS / f"{sid.strip().lstrip('#').upper()}.md"
    try:
        card = board.Card.load(path)
    except FileNotFoundError:
        raise SuggestionError('That suggestion is no longer waiting: it was kept, rejected or '
                              'removed. Refresh the list.') from None
    if card.type != 'memory' or card.status != 'suggested':
        raise SuggestionError('That file is not a pending memory suggestion.')
    return card


def accept(sid: str, *, fact: str | None = None, title: str | None = None,
           author: str = 'owner') -> dict:
    """Keep: write the active user memory (optionally edited) and drop the suggestion.

    Returns the Globals record of the saved card, as `globals_saved` carries it.
    """
    from .globals_protocol import GlobalsCommands   # it imports this module
    with _Lock():
        card = _pending_card(sid)
        text = _clean_fact(fact) if fact is not None else (_fact_of(card) or card.title)
        heading = _clean_title(title, text) if (title is not None or fact is not None) else card.title
        # A name the suggestion shares with an active memory is incidental — a real repeat was a
        # duplicate at suggest() — so the kept card takes the next free one instead of failing.
        taken = {str(c.front.get('name') or '').casefold() for c in memories.cards(aliases.global_root())}
        base = name = str(card.front.get('name') or slug(text))
        for n in range(2, 100):
            if name.casefold() not in taken:
                break
            name = f'{base}-{n}'
        front = dict(type='memory', status='active', name=name,
                     scope='user', pinned=True, paths=[],
                     source=str(card.front.get('source') or 'agent'), reviewed=_today())
        saved = board.Card(front=front, body=f'# {heading}\n\n{text}\n', dirty=True)
        events = []
        GlobalsCommands(events.append, author=author).dispatch(
            dict(type='globals_save', id='memory-suggestion', kind='memory', key=None,
                 text=saved.to_text(), base_hash=''))
        result = events[-1] if events else dict(event='globals_error', message='Nothing was saved.')
        if result['event'] != 'globals_saved':
            raise SuggestionError(result.get('message') or 'The memory could not be saved.')
        card.path.unlink()
        origin = str(card.front.get('origin') or '')
        try:
            board.Board(aliases.global_root()).append_thread(
                result['record']['key'],
                f"Kept from a suggestion by {front['source']}" + (f' ({origin})' if origin else '') + '.',
                author=author, kind='event')
        except (OSError, board.BoardError):
            pass   # the history line is a courtesy; the memory itself is saved
    return result['record']


def reject(sid: str, *, reason: str | None = None) -> dict:
    """No: move the suggestion to the rejected folder, where it declines its repeats."""
    if reason is not None and not isinstance(reason, str):
        raise SuggestionError('A reason is text.')
    with _Lock():
        card = _pending_card(sid)
        card.set('status', 'rejected')
        card.set('rejected', _today())
        reason = ' '.join((reason or '').split())[:400]
        if reason:
            card.set('reason', reason)
        target = _memory_dir() / REJECTED / card.path.name
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            raise SuggestionError('A rejection with this id already exists.')
        board.atomic_write(target, card.to_text())
        card.path.unlink()
        card.path = target
    return _record(card, 'rejected')
