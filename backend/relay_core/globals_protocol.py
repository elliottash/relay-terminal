# SPDX-License-Identifier: AGPL-3.0-or-later
"""Globals editor: existing Switchboard cards and original instruction sources."""
from __future__ import annotations

from . import filelock as fcntl
import hashlib
import os
from pathlib import Path
from . import aliases, board, instructions, memories, memory_suggestions

MAX_TEXT = 128 * 1024


class GlobalsCommands:
    def __init__(self, emit, workspace=lambda: None, *, author='owner'):
        self.emit, self.workspace = emit, workspace
        self.author = author

    def handles(self, kind):
        return kind in ('globals_list', 'globals_get', 'globals_save', 'globals_retire',
                        'globals_suggestions', 'globals_suggestion_accept', 'globals_suggestion_reject')

    def _records(self, workspace):
        root = aliases.global_root()
        records, problems = [], []
        local_aliases, _ = aliases.load(workspace, scopes=('local',))
        local_names = {a.name for a in local_aliases if a.status == 'active'}
        local_memories = {memories.identity(c) for c in memories.cards(memories.project_root(workspace))
                          if c.front.get('scope') != 'team'}
        for kind, folder in (('memory', 'memory'), ('alias', 'aliases')):
            # Pending and rejected memory suggestions (#MEMS) have their own list and requests.
            paths = [p for p in sorted((root / folder).glob('**/*.md'))
                     if not (kind == 'memory' and p.parent.name in (memory_suggestions.SUGGESTIONS,
                                                                    memory_suggestions.REJECTED)
                             and p.parent.parent == root / folder)]
            for path in paths[:memories.MAX_CARDS]:
                try:
                    if not path.resolve().is_relative_to(root.resolve()) or path.stat().st_size > MAX_TEXT:
                        raise ValueError('File is outside HQ or exceeds the editor size limit.')
                    card = board.Card.load(path)
                    if card.type != kind or not card.id:
                        raise ValueError('Card needs the expected type and an id.')
                    shadowed = (str(card.front.get('name')) in local_names if kind == 'alias'
                                else memories.identity(card) in local_memories)
                    records.append(dict(kind=kind, key=card.id, title=card.title, path=str(path),
                                        scope='global', status=card.status, shadowed=shadowed, exists=True))
                    if kind == 'memory':
                        records[-1].update(memory_scope=card.front.get('scope', 'user'),
                                           pinned=card.front.get('pinned') is True,
                                           paths=card.front.get('paths') or [],
                                           summary=' '.join(line.strip() for line in card.body.splitlines()
                                                            if line.strip() and not line.startswith('#'))[:240])
                except (OSError, ValueError, board.BoardError) as exc:
                    problems.append(dict(path=str(path), message=str(exc)))
        # Known sources only, including missing fixed paths so they can be created in place.
        sources = []
        seen = set()
        for tool, pattern in instructions.GLOBAL_CONVENTIONS:
            for path in instructions._expand(None, pattern):
                if str(path) not in seen:
                    seen.add(str(path))
                    sources.append(dict(path=str(path), tool=tool, exists=path.is_file()))
        target = str(instructions.default_target())
        if not any(r['path'] == target for r in sources):
            sources.append(dict(path=target, tool='Relay', exists=Path(target).is_file()))
        for row in sources:
            records.append(dict(kind='instruction', key=row['path'], path=row['path'],
                                title=f"{row['tool']}: {Path(row['path']).name}", scope='global',
                                status='source', shadowed=False, exists=row['exists']))
        return records, problems

    def _record(self, kind, key, workspace):
        records, _ = self._records(workspace)
        matches = [r for r in records if r['kind'] == kind and r['key'] == key]
        if len(matches) != 1:
            raise ValueError('Unknown or ambiguous Globals record. Refresh the list.')
        return matches[0]

    def _read(self, record):
        path = Path(record['path'])
        if path.exists() and path.stat().st_size > MAX_TEXT:
            raise ValueError('File exceeds the editor size limit.')
        exists = path.exists()
        data = path.read_bytes() if exists else b''
        if len(data) > MAX_TEXT:
            raise ValueError('File exceeds the editor size limit.')
        text = data.decode('utf-8')
        if '\x00' in text:
            raise ValueError('Cannot edit binary content.')
        return dict(record, text=text, hash=hashlib.sha256(data).hexdigest() if exists else '')

    def dispatch(self, request):
        ident = request.get('id')
        try:
            workspace = request.get('workspace') or self.workspace()
            kind, key = request.get('kind'), request.get('key')
            action = request['type']
            if action == 'globals_list':
                records, problems = self._records(workspace)
                self.emit(dict(event='globals_state', id=ident, root=str(aliases.global_root()),
                               records=records, problems=problems))
                return
            if action == 'globals_suggestions':
                self.emit(dict(event='globals_suggestions', id=ident,
                               pending=memory_suggestions.pending(),
                               rejected=memory_suggestions.rejected()))
                return
            if action == 'globals_suggestion_accept':
                record = memory_suggestions.accept(request.get('sid'), fact=request.get('text'),
                                                   title=request.get('title'), author=self.author)
                self.emit(dict(event='globals_suggestion_accepted', id=ident,
                               sid=str(request['sid']).strip().lstrip('#').upper(), record=record))
                return
            if action == 'globals_suggestion_reject':
                record = memory_suggestions.reject(request.get('sid'), reason=request.get('reason'))
                self.emit(dict(event='globals_suggestion_rejected', id=ident, sid=record['id'],
                               record=record))
                return
            if kind not in ('memory', 'alias', 'instruction'):
                raise ValueError('Unknown Globals record kind.')
            record = self._record(kind, key, workspace) if key else None
            if action == 'globals_get':
                if record is None:
                    raise ValueError('Choose a Globals record.')
                self.emit(dict(event='globals_record', id=ident, record=self._read(record)))
                return
            if action == 'globals_retire' and (record is None or kind == 'instruction'):
                raise ValueError('Only existing memories and aliases can be retired.')
            if not isinstance(request.get('base_hash'), str):
                raise ValueError('A base_hash is required. Reload before saving.')
            text = request.get('text', '')
            if not isinstance(text, str) or len(text.encode('utf-8')) > MAX_TEXT or '\x00' in text:
                raise ValueError('Text must be UTF-8 content of at most 128 KiB.')
            root = aliases.global_root()
            if kind == 'instruction':
                if record is None:
                    raise ValueError('Choose a known instruction source.')
                path = Path(record['path'])
                if path.is_symlink():
                    raise ValueError('This instruction source is a symbolic link; edit its target directly.')
            else:
                card = board.Card.parse(text)
                if action == 'globals_retire':
                    card = board.Card.load(Path(record['path']))
                    card.set('status', 'retired')
                if card.type != kind or not card.title or card.status not in ('active', 'retired'):
                    raise ValueError('Card needs a title, matching type, and active or retired status.')
                if record:
                    if card.id != key:
                        raise ValueError('Keep the existing card id.')
                    path = Path(record['path'])
                else:
                    new = board.new_card(kind, card.title, card.status)
                    for field in ('id', 'rank', 'created', 'links'):
                        card.set(field, new.front[field])
                    path = root / ('memory' if kind == 'memory' else 'aliases') / (card.id + '.md')
                if kind == 'alias':
                    alias = aliases.validate(aliases.from_card(card, 'global'))
                    card.set('name', alias.name)
                    if record is None:
                        path = root / 'aliases' / (alias.name + '.md')
                else:
                    if card.front.get('scope', 'user') not in ('user', 'project', 'team'):
                        raise ValueError('Memory scope must be user, project or team.')
                    if 'pinned' in card.front and type(card.front['pinned']) is not bool:
                        raise ValueError('Memory pinned must be true or false.')
                    for field in ('paths', 'supersedes'):
                        if field in card.front and (not isinstance(card.front[field], list) or
                                not all(isinstance(x, str) for x in card.front[field])):
                            raise ValueError(f'Memory {field} must be a list of strings.')
                records, _ = self._records(workspace)
                for other in records:
                    if other['kind'] == kind and other['key'] != card.id:
                        existing = board.Card.load(Path(other['path']))
                        if (card.front.get('name') and str(existing.front.get('name') or '').casefold() == str(card.front['name']).casefold()
                                and existing.status == 'active' and card.status == 'active'):
                            raise ValueError('An active record with this name already exists.')
                key = card.id
                text = card.to_text()
            path.parent.mkdir(parents=True, exist_ok=True)
            # Directory lock makes the optimistic check and replacement indivisible for HQ editors.
            lock = fcntl.open_directory_lock(path.parent)
            try:
                fcntl.flock(lock, fcntl.LOCK_EX)
                if board.file_hash(path) != request['base_hash']:
                    raise ValueError('This file changed since you opened it. Reload before saving.')
                if kind != 'instruction' and not (root / board.BOARD_CONFIG).exists():
                    root.mkdir(parents=True, exist_ok=True)
                    board.atomic_write(root / board.BOARD_CONFIG, board.CONFIG_TEXT)
                target = path
                if kind != 'instruction':
                    folder = root / ('memory' if kind == 'memory' else 'aliases')
                    filename = (card.front['name'] + '.md') if kind == 'alias' else path.name
                    target = folder / ('archive' if card.status == 'retired' else '') / filename
                    if target != path:
                        target.parent.mkdir(parents=True, exist_ok=True)
                        if target.exists():
                            raise ValueError('A record already occupies the target path.')
                board.atomic_write(target, text)
                if target != path:
                    path.unlink()
            finally:
                os.close(lock)
            if kind != 'instruction':
                board.Board(root).append_thread(key, 'Retired in Globals.' if action == 'globals_retire'
                                              else 'Saved in Globals.', author=self.author, kind='event')
            result = self._read(self._record(kind, key, workspace))
            self.emit(dict(event='globals_saved', id=ident, record=result))
        except (OSError, ValueError, TypeError, board.BoardError, aliases.AliasError) as exc:
            self.emit(dict(event='globals_error', id=ident, message=str(exc)))
