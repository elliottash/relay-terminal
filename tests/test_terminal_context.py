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


if __name__ == '__main__':
    unittest.main()
