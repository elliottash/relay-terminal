"""Capability and byte-boundary tests for the memory-only terminal service."""
import unittest

from relay_core.terminal_context import (
    MAX_EXCERPT_BYTES, MAX_OUTPUT_BYTES, TOOL_SPECS, Service, format_snapshot, validate_snapshot,
)


def record(command_id='one', **changes):
    value = dict(command_id=command_id, pane_id='pane-a', generation='login-a',
                 sequence=1, origin='user', command='printf hello', cwd='/tmp', host='',
                 started_at=1000, ended_at=2000, state='completed', exit_status=0,
                 output='hello', bytes_seen=5, revision=1, availability='captured')
    value.update(changes)
    if 'output' in changes and 'bytes_seen' not in changes:
        value['bytes_seen'] = len(value['output'].encode('utf-8'))
    return value


def snapshot(*records, mode='automatic'):
    return {'mode': mode, 'records': list(records)}


class TerminalContextTests(unittest.TestCase):
    def setUp(self):
        self.service = Service()

    def read(self, command_id='one', **args):
        return self.service.execute('terminal_read', {'command_id': command_id, **args})

    def test_only_selected_records_are_grants(self):
        one, other = record(), record('other', pane_id='pane-b')
        self.service.update(snapshot(one, other))
        self.service.set_snapshot(snapshot(one))
        self.assertEqual(self.read()['output'], 'hello')
        self.assertFalse(self.read('other')['ok'])
        self.assertEqual(self.read('never')['error'], 'unknown')
        history = self.service.execute('terminal_history', {})['records']
        self.assertEqual([r['command_id'] for r in history], ['one'])
        self.assertNotIn('output', history[0])

    def test_removal_and_next_empty_turn_revoke(self):
        self.service.update(snapshot(record()))
        self.service.set_snapshot(snapshot(record()))
        self.service.set_snapshot(snapshot())
        self.assertEqual(self.read()['error'], 'revoked')
        self.service.set_snapshot(None)
        self.assertEqual(self.service.execute('terminal_history', {})['error'], 'revoked')

    def test_manual_revokes_automatic_but_allows_explicit_selection(self):
        self.service.update(snapshot(record()))
        self.service.set_snapshot(snapshot(record()))
        self.service.update(snapshot(record(), mode='manual'))
        self.assertEqual(self.read()['error'], 'revoked')
        # Even a queued old automatic snapshot cannot reverse live policy.
        self.service.set_snapshot(snapshot(record()))
        self.assertEqual(self.read()['error'], 'revoked')
        self.service.set_snapshot(snapshot(record(), mode='manual'))
        self.assertEqual(self.read()['output'], 'hello')

    def test_off_revokes_even_queued_old_snapshot(self):
        old = snapshot(record())
        self.service.update(old)
        self.service.set_snapshot(old)
        self.service.update(snapshot(mode='off'))
        self.assertEqual(self.read()['error'], 'revoked')
        self.service.set_snapshot(old)
        self.assertEqual(self.read()['error'], 'revoked')
        self.assertEqual(self.service.execute('terminal_history', {})['error'], 'revoked')

    def test_queue_pins_revision_and_copies_inputs_and_results(self):
        old = snapshot(record())
        self.service.update(old)
        self.service.set_snapshot(old)
        old['records'][0]['output'] = 'mutated'
        self.service.update(snapshot(record(output='later', revision=2)))
        result = self.read()
        self.assertEqual(result['output'], 'hello')
        result['record']['revision'] = 999
        self.assertEqual(self.read()['record']['revision'], 1)
        self.assertEqual(self.read(fresh=True)['output'], 'later')
        self.assertEqual(self.read(fresh=True)['record']['revision'], 2)

    def test_eviction_and_identity_reuse(self):
        self.service.update(snapshot(record()))
        self.service.set_snapshot(snapshot(record()))
        self.service.update(snapshot(record('replacement')))
        self.assertEqual(self.read()['output'], 'hello')
        self.assertEqual(self.service.snapshot()['records'][0]['output'], 'hello')
        self.assertEqual(self.read(fresh=True)['error'], 'evicted')
        self.assertEqual(self.service.execute('terminal_history', {})['records'][0]['command_id'], 'one')
        for change in ({'pane_id': 'pane-b'}, {'generation': 'login-b'}):
            self.service.update(snapshot(record(**change)))
            self.assertEqual(self.read()['error'], 'revoked')
            self.assertEqual(self.read(fresh=True)['error'], 'revoked')

    def test_snapshot_without_live_update_and_stale_fresh(self):
        self.service.set_snapshot(snapshot(record(revision=2)))
        self.assertEqual(self.read()['output'], 'hello')
        self.assertEqual(self.read(fresh=True)['error'], 'unknown')
        self.service.update(snapshot(record(revision=1)))
        self.assertEqual(self.read(fresh=True)['error'], 'stale')

    def test_schema_rejects_malformed_and_out_of_bounds_atomically(self):
        good = snapshot(record())
        self.service.update(good)
        self.service.set_snapshot(good)
        bad = [snapshot(record(output='x' * (MAX_OUTPUT_BYTES + 1))),
               snapshot(*[record(str(i)) for i in range(33)]),
               snapshot(record(), record()), snapshot(record(), mode='off'),
               snapshot(record(revision=True)), snapshot(record(ended_at=0)),
               snapshot(record(state='running')),
               snapshot(record(command_id='')), snapshot(record(host='\ud800')),
               snapshot(record(origin=[])), {'mode': 'automatic', 'records': [], 'extra': 1}]
        # Payload bound includes all metadata, not just output bytes.
        bad.append(snapshot(*[record(str(i), output='x' * MAX_OUTPUT_BYTES) for i in range(32)]))
        for payload in bad:
            with self.subTest(payload=str(payload)[:100]):
                with self.assertRaises(ValueError):
                    validate_snapshot(payload)
                with self.assertRaises(ValueError):
                    self.service.update(payload)
                self.assertEqual(self.read()['output'], 'hello')
        detached = validate_snapshot(good)
        detached['records'][0]['output'] = 'changed'
        self.assertEqual(good['records'][0]['output'], 'hello')

    def test_unicode_pagination_has_no_split_or_duplicate_characters(self):
        output = '🙂é漢' * 2000
        self.service.set_snapshot(snapshot(record(output=output)))
        cursor, chunks = 0, []
        while True:
            result = self.read(offset=cursor, limit=101)
            self.assertTrue(result['ok'])
            self.assertLessEqual(len(result['output'].encode('utf-8')), 101)
            chunks.append(result['output'])
            cursor = result['next_offset']
            if result['eof']:
                break
        self.assertEqual(''.join(chunks), output)
        self.assertEqual(self.read(limit=1)['error'], 'invalid_limit')
        self.assertEqual(self.read(offset=len(output) + 1)['error'], 'invalid_offset')
        self.assertEqual(self.read(offset=len(output))['output'], '')

    def test_effective_provider_snapshot_honors_revocation_and_is_detached(self):
        old = snapshot(record())
        self.service.update(old)
        self.service.set_snapshot(old)
        effective = self.service.snapshot()
        effective['records'][0]['output'] = 'tampered'
        self.assertEqual(self.read()['output'], 'hello')
        other_worker = Service()
        self.assertFalse(other_worker.execute('terminal_read', {'command_id': 'one'})['ok'])
        self.service.update(snapshot(mode='off'))
        self.service.set_snapshot(old)
        self.assertEqual(format_snapshot(self.service.snapshot()), '')

    def test_snapshot_is_sanitized_and_still_valid_when_redaction_expands(self):
        self.service.set_snapshot(snapshot(record(output='token=x', command='password=x')))
        effective = self.service.snapshot()
        self.assertNotIn('token=x', str(effective))
        self.assertNotIn('password=x', str(effective))
        self.assertEqual(validate_snapshot(effective), effective)
        self.assertTrue(format_snapshot(effective))

    def test_raw_byte_count_can_be_smaller_than_sanitized_capture(self):
        r = record(output='\ufffd[output omitted]', bytes_seen=1, availability='truncated')
        self.assertEqual(validate_snapshot(snapshot(r))['records'][0]['bytes_seen'], 1)
        self.service.update(snapshot(r))
        self.service.set_snapshot(snapshot(r))
        self.assertEqual(self.read()['output'], r['output'])

    def test_preview_is_nonmutating_and_applies_live_policy_and_identity(self):
        active, proposed = record(), record('steer', output='token=x')
        self.service.update(snapshot(active, proposed))
        self.service.set_snapshot(snapshot(active))
        preview = self.service.preview_snapshot(snapshot(proposed))
        self.assertEqual(preview['records'][0]['command_id'], 'steer')
        self.assertNotIn('token=x', str(preview))
        self.assertEqual(self.read()['output'], 'hello')
        self.assertFalse(self.read('steer')['ok'])
        preview['records'].clear()
        self.assertEqual(self.service.snapshot()['records'][0]['command_id'], 'one')
        self.service.update(snapshot(active, record('steer', generation='other')))
        self.assertEqual(self.service.preview_snapshot(snapshot(proposed))['records'], [])
        self.service.update(snapshot(active, proposed, mode='manual'))
        self.assertEqual(self.service.preview_snapshot(snapshot(proposed))['records'], [])
        self.assertEqual(self.service.preview_snapshot(snapshot(proposed, mode='manual'))['records'][0]['command_id'], 'steer')
        self.service.update(snapshot(mode='off'))
        self.assertEqual(self.service.preview_snapshot(snapshot(proposed, mode='manual')),
                         {'mode': 'off', 'records': []})
        self.assertEqual(self.service.preview_snapshot(None), {'mode': 'off', 'records': []})

    def test_tool_specs_follow_relay_function_contract(self):
        self.assertEqual([spec['function']['name'] for spec in TOOL_SPECS],
                         ['terminal_history', 'terminal_read'])
        for spec in TOOL_SPECS:
            self.assertEqual(spec['type'], 'function')
            function = spec['function']
            self.assertTrue(function['description'])
            self.assertEqual(function['parameters']['type'], 'object')
            self.assertFalse(function['parameters']['additionalProperties'])
        read = TOOL_SPECS[1]['function']['parameters']
        self.assertEqual(read['required'], ['command_id'])
        self.assertEqual(read['properties']['limit']['maximum'], 16384)

    def test_tool_argument_validation(self):
        self.service.set_snapshot(snapshot(record()))
        for args in ({'limit': 16385}, {'offset': True}, {'limit': 0},
                     {'fresh': 'yes'}, {'offset': -1}, {'path': '/etc/passwd'}):
            self.assertEqual(self.read(**args)['error'], 'invalid_arguments')
        self.assertEqual(self.service.execute('terminal_history', {'fresh': True})['error'], 'invalid_arguments')
        self.assertEqual(self.service.execute('terminal_read', [])['error'], 'invalid_arguments')
        self.assertEqual(self.service.execute('execute_shell', {})['error'], 'unknown_tool')

    def test_render_cap_head_tail_and_secret_filter(self):
        output = 'FIRST password=supersecret\n' + '🙂' * 15000 + '\nLAST'
        payload = snapshot(record(output=output, availability='truncated'))
        rendered = format_snapshot(payload)
        self.assertLessEqual(len(rendered.encode('utf-8')), MAX_EXCERPT_BYTES)
        for expected in ('untrusted evidence', 'FIRST', 'LAST', 'truncated', 'pane-a'):
            self.assertIn(expected, rendered)
        self.assertNotIn('supersecret', rendered)
        self.service.set_snapshot(payload)
        self.assertNotIn('supersecret', self.read()['output'])
        many = snapshot(*[record(str(i), output='漢' * 18000) for i in range(32)])
        self.assertLessEqual(len(format_snapshot(many).encode('utf-8')), MAX_EXCERPT_BYTES)
        self.assertEqual(format_snapshot(None), '')

    def test_empty_unsupported_interrupted_and_running_remain_distinct(self):
        for changes in ({'output': ''}, {'output': '', 'availability': 'unsupported'},
                        {'state': 'interrupted', 'availability': 'interrupted', 'exit_status': None},
                        {'state': 'running', 'ended_at': None, 'exit_status': None}):
            r = record(**changes)
            self.service.set_snapshot(snapshot(r))
            got = self.read()
            self.assertEqual(got['record']['state'], r['state'])
            self.assertEqual(got['record']['availability'], r['availability'])
            self.assertEqual(got['record']['exit_status'], r['exit_status'])


class TurnStartResolutionTests(unittest.TestCase):
    """#XCXD: automatic terminal context resolves when the turn starts, not when it queues."""

    def setUp(self):
        self.service = Service()

    def eligible(self, *records):
        return {'mode': 'automatic', 'source': 'automatic',
                'scope': {'pane_id': 'pane-a', 'generation': 'login-a'}, 'records': list(records)}

    def test_source_and_scope_field_rules(self):
        self.assertEqual(validate_snapshot(snapshot(record())), {'mode': 'automatic', 'records': [record()]})
        pinned = {'mode': 'automatic', 'source': 'pinned', 'records': [record()]}
        self.assertEqual(validate_snapshot(pinned)['source'], 'pinned')
        # A refresh-eligible snapshot must say which pane/generation it belongs to, and
        # only it may carry a scope: legacy and pinned snapshots never do.
        self.assertEqual(self.eligible(record())['records'], [record()])
        self.assertEqual(validate_snapshot(self.eligible(record()))['scope'],
                         {'pane_id': 'pane-a', 'generation': 'login-a'})
        with self.assertRaises(ValueError):
            validate_snapshot({'mode': 'automatic', 'source': 'automatic', 'records': []})
        with self.assertRaises(ValueError):
            validate_snapshot({'mode': 'automatic', 'source': 'pinned',
                               'scope': {'pane_id': 'a', 'generation': 'b'}, 'records': []})
        with self.assertRaises(ValueError):
            validate_snapshot({'mode': 'automatic', 'records': [],
                               'scope': {'pane_id': 'a', 'generation': 'b'}})
        with self.assertRaises(ValueError):
            validate_snapshot(self.eligible() | {'scope': {'pane_id': '', 'generation': 'b'}})
        with self.assertRaises(ValueError):
            validate_snapshot(self.eligible() | {'scope': {'pane_id': 'a'}})

    def test_legacy_snapshots_never_expand(self):
        running = record('run', state='running', ended_at=None, exit_status=None, revision=0)
        self.service.update(snapshot(running))
        done = record('run', revision=1, output='hello there', bytes_seen=11)
        self.service.update(snapshot(done, record('later', sequence=2)))
        self.assertEqual(self.service.resolve_turn_snapshot(snapshot(running))['records'], [running])
        pinned = {'mode': 'automatic', 'source': 'pinned', 'records': [running]}
        self.assertEqual(self.service.resolve_turn_snapshot(pinned)['records'], [running])

    def test_queued_running_command_reports_result_at_turn_start(self):
        running = record('run', state='running', ended_at=None, exit_status=None, revision=0, output='hel')
        self.service.update(snapshot(running))
        done = record('run', revision=1, output='hello', bytes_seen=5, ended_at=2000, exit_status=0)
        self.service.update(snapshot(done))
        resolved = self.service.resolve_turn_snapshot(self.eligible(running))
        self.assertEqual(resolved['records'], [done])
        self.service.set_snapshot(resolved)
        self.assertEqual(self.service.execute('terminal_read', {'command_id': 'run'})['output'], 'hello')

    def test_running_output_growth_replaces_queued_value(self):
        running = record('run', state='running', ended_at=None, exit_status=None, revision=0,
                         output='hel', bytes_seen=3)
        self.service.update(snapshot(running))
        grown = record('run', state='running', ended_at=None, exit_status=None, revision=0,
                       output='hello', bytes_seen=5)
        self.service.update(snapshot(grown))
        resolved = self.service.resolve_turn_snapshot(self.eligible(running))
        self.assertEqual(resolved['records'][0]['bytes_seen'], 5)
        self.assertIn('running/incomplete', format_snapshot(resolved))

    def test_only_changes_since_last_started_turn_are_shared(self):
        first = record('first')
        self.service.update(snapshot(first))
        self.service.set_snapshot(self.eligible(first))
        # Commands that ran after that turn started are new to the next one; the
        # unchanged completed record the started turn carried is not re-sent.
        second, third = record('second', sequence=2), record('third', sequence=3)
        self.service.update(snapshot(first, second, third))
        resolved = self.service.resolve_turn_snapshot(self.eligible(first))
        self.assertEqual([r['command_id'] for r in resolved['records']], ['second', 'third'])
        self.service.set_snapshot(resolved)
        # Nothing changed since: nothing is re-sent.
        self.assertEqual(self.service.resolve_turn_snapshot(self.eligible(third))['records'], [])

    def test_consecutive_auto_turns_do_not_repeat_unchanged_completed_output(self):
        done = record('done')
        running = record('run', state='running', ended_at=None, exit_status=None, revision=0)
        self.service.update(snapshot(done, running))
        queued = self.eligible(done, running)
        first = self.service.resolve_turn_snapshot(queued)
        self.assertEqual([r['command_id'] for r in first['records']], ['done', 'run'])
        self.service.set_snapshot(first)
        second = self.service.resolve_turn_snapshot(queued)
        self.assertEqual([r['command_id'] for r in second['records']], ['run'])
        self.assertIn('running/incomplete', format_snapshot(second))

    def test_old_history_does_not_crowd_out_new_output(self):
        old = [record(f'old{i}', sequence=i) for i in range(30)]
        self.service.update(snapshot(*old))
        self.service.set_snapshot(self.eligible(*old[:2]))
        fresh = record('fresh', sequence=99)
        self.service.update(snapshot(fresh, *old))
        resolved = self.service.resolve_turn_snapshot(self.eligible(old[0]))
        self.assertEqual([r['command_id'] for r in resolved['records']], ['fresh'])

    def test_agent_origin_and_foreign_scopes_never_leak(self):
        mine = record('mine')
        self.service.update(snapshot(mine))
        self.service.set_snapshot(self.eligible(mine))
        agent_run = record('agent-run', origin='agent', sequence=50)
        other_pane = record('other-pane', pane_id='pane-b', sequence=51)
        other_gen = record('other-gen', generation='login-b', sequence=52)
        new_mine = record('new-mine', sequence=53)
        self.service.update(snapshot(mine, agent_run, other_pane, other_gen, new_mine))
        # `mine` is unchanged completed and already carried by the started turn; the new
        # in-scope user command is shared, agent-origin and foreign scopes are not.
        resolved = self.service.resolve_turn_snapshot(self.eligible(mine))
        self.assertEqual([r['command_id'] for r in resolved['records']], ['new-mine'])
        # A queued value is never replaced from another pane or generation either.
        foreign = record('mine', pane_id='pane-b', revision=9)
        self.service.update(snapshot(foreign))
        self.assertEqual(self.service.resolve_turn_snapshot(self.eligible(new_mine))['records'], [new_mine])

    def test_off_and_manual_mirrors_return_the_queued_snapshot(self):
        running = record('run', state='running', ended_at=None, exit_status=None, revision=0)
        done = record('run', revision=1)
        queued = self.eligible(running)
        self.service.update(snapshot(done, mode='manual'))
        self.assertEqual(self.service.resolve_turn_snapshot(queued)['records'], [running])
        self.service.update({'mode': 'off', 'records': []})
        self.assertEqual(self.service.resolve_turn_snapshot(queued)['records'], [running])

    def test_off_window_does_not_mark_unseen_results_seen(self):
        running = record('run', state='running', ended_at=None, exit_status=None, revision=0)
        self.service.update(snapshot(running))
        self.service.set_snapshot(self.eligible(running))
        # A turn starts while sharing is off: it is granted nothing and sees nothing.
        self.service.update({'mode': 'off', 'records': []})
        self.service.set_snapshot(self.eligible(running))
        # Sharing returns; both the finished command and one that ran behind the off
        # window are new to the next started turn.
        done = record('run', revision=1, output='ok', exit_status=0)
        behind = record('behind', sequence=9, revision=1)
        self.service.update(snapshot(done, behind))
        resolved = self.service.resolve_turn_snapshot(self.eligible(running))
        self.assertEqual([r['command_id'] for r in resolved['records']], ['run', 'behind'])
        self.assertEqual(resolved['records'][0]['exit_status'], 0)

    def test_queued_ahead_of_the_command_uses_the_snapshots_scope(self):
        started = record('started', state='running', ended_at=None, exit_status=None, revision=0)
        foreign = record('foreign', pane_id='pane-b', sequence=2)
        agent = record('agent', origin='agent', sequence=3)
        self.service.update(snapshot(started, foreign, agent))
        # Empty selection at submission: the snapshot's explicit scope anchors it, and
        # nothing is inferred from the mirror.
        resolved = self.service.resolve_turn_snapshot(self.eligible())
        self.assertEqual([r['command_id'] for r in resolved['records']], ['started'])

    def test_first_turn_after_queueing_sees_command_that_completed(self):
        # The prompt queued before the pane's first command existed; the command
        # finished before the turn ever started. Its result and exit status are the
        # turn's terminal context — an empty selection is not.
        done = record('run', revision=1, output='done', exit_status=0)
        self.service.update(snapshot(done))
        resolved = self.service.resolve_turn_snapshot(self.eligible())
        self.assertEqual([r['command_id'] for r in resolved['records']], ['run'])
        self.assertEqual(resolved['records'][0]['exit_status'], 0)
        self.service.set_snapshot(resolved)
        self.assertEqual(self.service.execute('terminal_read', {'command_id': 'run'})['output'], 'done')

    def test_stale_queued_selection_not_resent_after_acceptance(self):
        old = record('run', revision=1, output='first')
        self.service.update(snapshot(old))
        queued = self.eligible(old)
        fresh = record('run', revision=2, output='more')
        self.service.update(snapshot(fresh))
        first = self.service.resolve_turn_snapshot(queued)
        self.assertEqual(first['records'][0]['revision'], 2)
        self.service.set_snapshot(first)
        # An older queued entry still holding revision 1 must not resurrect revision 2
        # output the started turn already carried.
        self.assertEqual(self.service.resolve_turn_snapshot(queued)['records'], [])


if __name__ == '__main__':
    unittest.main()
