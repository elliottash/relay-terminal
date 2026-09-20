"""Request ledger, todos, drop-path regressions (research G1-G3, G5, G7), carried compaction block,
completion check, stale todo reminders and the audit side call. Stub providers only: no network, and the
keyring and route_assist.router_provider are patched wherever the audit could reach them."""
import json
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

from relay_core import context, requests as ledger_mod, route_assist, sidecall, todos as todo_mod
from relay_core.agent import Agent
from relay_core.provider import Cancelled, ProviderConfig, wire_messages
from relay_core.queue import TurnSupervisor
from relay_core.session_protocol import SessionCommands
from relay_core.sessions import SessionStore
from test_queue import Recorder

CONFIG = lambda: ProviderConfig('http://127.0.0.1:12345/v1', 'mock', '')  # noqa: E731
_ids = iter(range(1, 10**9))


def call(name, arguments, call_id=None):
    return {'id': call_id or f'c{next(_ids)}', 'type': 'function',
            'function': {'name': name, 'arguments': json.dumps(arguments)}}


def tools_msg(*calls):
    return {'role': 'assistant', 'content': '', 'reasoning_content': 'r', 'tool_calls': list(calls)}


def text(content):
    return {'role': 'assistant', 'content': content}


def todos_call(*items):
    return tools_msg(call('update_todos', {'items': list(items)}))


class Script:
    """Tool-enabled calls pop scripted responses (callables receive the messages); when empty, `default`.
    No-tools calls (summaries, audit) return side_reply."""
    def __init__(self, responses=(), default=None, side_reply='SUMMARY'):
        self.responses, self.default, self.side_reply = list(responses), default, side_reply
        self.requests, self.side_requests = [], []

    def complete(self, messages, tools, emit, cancel):
        if cancel.is_set():
            raise Cancelled('Stopped.')
        snapshot = json.loads(json.dumps(messages))
        if not tools:
            self.side_requests.append(snapshot)
            return text(self.side_reply)
        self.requests.append(snapshot)
        if self.responses:
            response = self.responses.pop(0)
        elif self.default is not None:
            response = self.default
        else:
            response = text('ok')
        return response(snapshot) if callable(response) else response

    def cancel(self):
        pass


def forever_tools(_messages):
    return tools_msg(call('run_command', {'command': 'true'}))


class Base(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name) / 'ws'
        self.root.mkdir()
        self.sessions = Path(self.temp.name) / 'sessions'
        self.events = []
        # The audit must never reach the keyring or the network in tests.
        patcher = mock.patch.object(route_assist, 'router_provider', side_effect=AssertionError('router used'))
        self.router = patcher.start()
        self.addCleanup(patcher.stop)
        keyring = mock.patch('relay_core.keystore.lookup', side_effect=AssertionError('keyring used'))
        keyring.start()
        self.addCleanup(keyring.stop)

    def tearDown(self):
        self.temp.cleanup()

    def agent(self, provider, **kw):
        kw.setdefault('session_dir', str(self.sessions))
        return Agent(CONFIG(), str(self.root), self.events.append, provider=provider, **kw)

    def of(self, kind):
        return [e for e in self.events if e.get('event') == kind]


# ----- unit: ledger, todos, carried block, transcript, audit parsing -------------------------------
class LedgerUnitTests(unittest.TestCase):
    def test_lifecycle_and_todo_derivation(self):
        changes = []
        ledger = ledger_mod.RequestLedger(on_change=lambda: changes.append(1))
        r1 = ledger.add('fix X', 'ask')
        r2 = ledger.add('also rename Y', 'steer')
        self.assertEqual((r1['id'], r2['id'], r1['status']), ('R1', 'R2', 'open'))
        ledger.deliver('R1', 't1', 1)
        ledger.deliver('R2', 't1', 1)
        self.assertEqual(ledger.get('R1')['status'], 'in_progress')
        todos = [{'id': 'T1', 'text': 'fix X', 'status': 'completed', 'request_ids': ['R1'], 'note': None},
                 {'id': 'T2', 'text': 'rename Y', 'status': 'deferred', 'request_ids': ['R2'], 'note': 'needs API'}]
        ledger.apply_todos(todos)
        self.assertEqual(ledger.get('R1')['status'], 'done')
        self.assertEqual((ledger.get('R2')['status'], ledger.get('R2')['reason']), ('deferred', 'needs API'))
        self.assertTrue(changes)

    def test_finish_turn_marks_unlinked_done_and_keeps_open_linked(self):
        ledger = ledger_mod.RequestLedger()
        for text_ in ('a', 'b'):
            ledger.add(text_, 'ask')
        ledger.deliver('R1', 't', 1)
        ledger.deliver('R2', 't', 1)
        open_todo = [{'id': 'T1', 'text': 'b', 'status': 'pending', 'request_ids': ['R2'], 'note': None}]
        ledger.finish_turn('t', True, open_todo)
        self.assertEqual((ledger.get('R1')['status'], ledger.get('R1')['handled']), ('done', True))
        self.assertEqual(ledger.get('R2')['status'], 'open')
        ledger.deliver('R2', 't2', 2)
        ledger.finish_turn('t2', False, [])
        self.assertEqual(ledger.get('R2')['status'], 'open')

    def test_user_status_sticks_and_validation(self):
        ledger = ledger_mod.RequestLedger()
        ledger.add('a', 'queue')
        with self.assertRaises(ValueError):
            ledger.set_status('R1', 'blocked')
        with self.assertRaises(ValueError):
            ledger_mod.check_ledger_id('3')
        ledger.set_status('R1', 'cancelled_by_user', 'not needed')
        ledger.deliver('R1', 't', 1)
        self.assertEqual(ledger.get('R1')['status'], 'cancelled_by_user')

    def test_round_trip_backfill_and_export(self):
        ledger = ledger_mod.RequestLedger()
        for i in range(3):
            ledger.add(f'p{i}', 'ask')
            ledger.deliver(f'R{i + 1}', f't{i}', i + 5)
        ledger.add('queued, never delivered', 'queue')
        copy_ = ledger_mod.RequestLedger()
        copy_.load_json(json.loads(json.dumps(ledger.to_json())))
        self.assertEqual([i['id'] for i in copy_.items], ['R1', 'R2', 'R3', 'R4'])
        self.assertEqual(copy_.next_id, 5)
        exported = ledger.export(6, {5: 1, 6: 2})
        self.assertEqual([(i['id'], i['turn']) for i in exported['items']], [('R1', 1), ('R2', 2)])
        legacy = ledger_mod.RequestLedger()
        legacy.backfill([{'turn': 1, 'prompt': 'old ask', 'time': 1.0}])
        self.assertEqual((legacy.items[0]['id'], legacy.items[0]['status']), ('R1', 'done'))

    def test_prune_keeps_open_entries(self):
        ledger = ledger_mod.RequestLedger()
        with mock.patch.object(ledger_mod, 'MAX_ITEMS', 5):
            first = ledger.add('stay open', 'ask')
            for i in range(8):
                item = ledger.add(f'x{i}', 'ask')
                ledger.set_status(item['id'], 'done')
            self.assertEqual(len(ledger.items), 5)
            self.assertIn(first, ledger.items)


class TodoUnitTests(unittest.TestCase):
    def test_several_todos_may_be_in_progress(self):
        # Owner, 2026-09-18 (#QHR1): no "only one task in progress" rule.
        items, _ = todo_mod.validate({'items': [{'text': 'a', 'status': 'in_progress'},
                                                {'text': 'b', 'status': 'in_progress'},
                                                {'text': 'c', 'status': 'in_progress'}]}, {'R1'}, [], 1)
        self.assertEqual([t['status'] for t in items], ['in_progress'] * 3)
        self.assertNotIn('Exactly one', todo_mod.SPEC['function']['description'])
        self.assertNotIn('exactly one todo', todo_mod.RULES)
        self.assertIn('several may be in progress at once', todo_mod.RULES)

    def test_validation(self):
        known = {'R1', 'R2'}
        cases = [
            {'items': [{'text': 'a', 'status': 'cancelled'}]},
            {'items': [{'text': 'a', 'status': 'pending', 'request_ids': ['R9']}]},
            {'items': [{'text': '', 'status': 'pending'}]},
            {'items': [{'text': 'a', 'status': 'done'}]},
            {'items': [{'text': 'a', 'status': 'pending', 'extra': 1}]},
            {'todos': []},
        ]
        for raw in cases:
            with self.assertRaises(ValueError, msg=raw):
                todo_mod.validate(raw, known, [], 1)

    def test_ids_are_kept_and_links_default(self):
        todos = todo_mod.TodoList()
        items = todos.replace({'items': [{'text': 'a', 'status': 'in_progress'},
                                         {'text': 'b', 'status': 'pending', 'request_ids': ['R2']}]},
                              {'R1', 'R2'}, 't1', ['R1'])
        self.assertEqual([(i['id'], i['request_ids']) for i in items], [('T1', ['R1']), ('T2', ['R2'])])
        items = todos.replace({'items': [{'id': 'T2', 'text': 'b', 'status': 'completed'},
                                         {'id': 'T77', 'text': 'c', 'status': 'blocked', 'note': 'no access'}]},
                              {'R1', 'R2', 'R3'}, 't2', ['R3'])
        self.assertEqual([(i['id'], i['request_ids']) for i in items], [('T2', ['R2']), ('T3', ['R3'])])
        restored = todo_mod.TodoList()
        restored.load_json(todos.to_json())
        self.assertEqual((restored.items, restored.next_id), (todos.items, 4))

    def test_a_refining_message_joins_the_existing_todo(self):
        """The prompt tells the model to add the new request id to a todo it already has rather
        than adding one (todos.RULES). Both requests then settle with that todo."""
        ledger = ledger_mod.RequestLedger()
        ledger.add('add a button', 'ask')
        ledger.deliver('R1', 't1', 1)
        todos = todo_mod.TodoList()
        todos.replace({'items': [{'text': 'add a button', 'status': 'in_progress'}]}, {'R1'}, 't1', ['R1'])
        # "make it blue" arrives mid-turn: a refinement, not new work.
        ledger.add('make it blue', 'steer')
        ledger.deliver('R2', 't1', 1)
        items = todos.replace({'items': [{'id': 'T1', 'text': 'add a blue button', 'status': 'in_progress',
                                          'request_ids': ['R1', 'R2']}]},
                              {'R1', 'R2'}, 't1', ['R2'])
        self.assertEqual([(i['id'], i['request_ids']) for i in items], [('T1', ['R1', 'R2'])])
        ledger.apply_todos(items)
        self.assertEqual([ledger.get(r)['status'] for r in ('R1', 'R2')], ['in_progress', 'in_progress'])
        items = todos.replace({'items': [{'id': 'T1', 'text': 'add a blue button', 'status': 'completed',
                                          'request_ids': ['R1', 'R2']}]},
                              {'R1', 'R2'}, 't1', ['R2'])
        ledger.apply_todos(items)
        self.assertEqual([ledger.get(r)['status'] for r in ('R1', 'R2')], ['done', 'done'])
        # One task, not two: the refinement never became its own todo.
        self.assertEqual(len(todos.items), 1)
        self.assertEqual(todos.next_id, 2)


class CarriedBlockTests(unittest.TestCase):
    def test_block_contents(self):
        requests = [
            {'id': 'R1', 'text': 'first ask ' + 'd' * 600, 'status': 'done', 'source': 'ask', 'reason': None,
             'requires_completion': True, 'handled': True},
            {'id': 'R2', 'text': 'also update the docs', 'status': 'open', 'source': 'steer', 'reason': None,
             'requires_completion': True, 'handled': False},
            {'id': 'R3', 'text': 'rename Y', 'status': 'done', 'source': 'steer', 'reason': None,
             'requires_completion': True, 'handled': True},
            {'id': 'R4', 'text': 'current ask', 'status': 'in_progress', 'source': 'ask', 'reason': None,
             'requires_completion': True, 'handled': False},
        ]
        todos = [{'id': 'T1', 'text': 'docs', 'status': 'pending', 'request_ids': ['R2'], 'note': None}]
        block = context.carried_block(requests, todos, open_budget_chars=10_000, recent=['recent verbatim message'],
                                      in_tail={'R4'}, plan_path='/p/plan.md', files=['/w/a.py'],
                                      subagents=[{'id': 'a1', 'type': 'explore', 'description': 'scan', 'status': 'running'}])
        self.assertTrue(block.startswith(context.CARRIED_MARKER))
        self.assertIn('> also update the docs', block)                      # open: verbatim
        self.assertIn('more characters; request finished', block)          # done: capped at 400
        self.assertIn('R3 [done, steer, handled: do not act on it again]', block)
        self.assertIn('R4 [in_progress] (text in the conversation below)', block)
        self.assertIn('- T1 [pending] docs -> R2', block)
        for part in ('/p/plan.md', '/w/a.py', 'a1 (explore)', '> recent verbatim message'):
            self.assertIn(part, block)

    def test_recent_user_messages_budget_and_skips(self):
        region = [{'role': 'user', 'content': 'old ' * 100, 'relay_kind': 'prompt', 'relay_requests': ['R1']},
                  {'role': 'user', 'content': 'note', 'relay_kind': 'note'},
                  {'role': 'user', 'content': 'open one', 'relay_kind': 'prompt', 'relay_requests': ['R2']},
                  {'role': 'user', 'content': 'newest', 'relay_kind': 'steer', 'relay_requests': ['R3']}]
        texts, used, ids = context.recent_user_messages(region, 1000, {'R2'})
        self.assertEqual(texts, ['old ' * 100, 'newest'])
        self.assertEqual(ids, {'R1', 'R3'})
        texts, used, _ = context.recent_user_messages(region, 250, set())
        self.assertEqual(texts[1:], ['open one', 'newest'])
        self.assertIn('[…middle trimmed…]', texts[0])   # the message that does not fit keeps head and tail
        self.assertLessEqual(used, 250)


class TranscriptTests(unittest.TestCase):
    def test_g7_user_messages_are_not_trimmed_for_the_summarizer(self):
        ask = 'START ' + ' '.join(f'ask{i}' for i in range(2000)) + ' END'
        messages = [{'role': 'user', 'content': ask}] + [
            {'role': 'tool', 'tool_call_id': 'x', 'content': 'noise ' * 1000} for _ in range(30)]
        old = sidecall.render_transcript(messages, max_chars=20_000)
        self.assertNotIn('ask1000 ', old)
        new = sidecall.render_transcript(messages, max_chars=20_000, keep_user=True)
        self.assertIn('ask1000 ', new)
        self.assertIn('START', new)
        self.assertLessEqual(len(new), 20_000 + 100)


class AuditParseTests(unittest.TestCase):
    def test_parse(self):
        reply = '```json\n{"unaddressed": [{"request_id": "R2", "quote": "also update the docs"},' \
                ' {"request_id": "R9", "quote": "made up"}, {"request_id": "R2", "quote": "also update the docs"},' \
                ' {"request_id": "R1"}]}\n```'
        self.assertEqual(ledger_mod.parse_audit(reply, ['R1', 'R2']),
                         [{'request_id': 'R2', 'quote': 'also update the docs'}])
        self.assertEqual(ledger_mod.parse_audit('{"unaddressed": []}', ['R1']), [])
        with self.assertRaises(ValueError):
            ledger_mod.parse_audit('all good', ['R1'])

    def test_wire_messages_strip_relay_keys(self):
        messages = [{'role': 'user', 'content': 'x', 'relay_kind': 'prompt', 'relay_requests': ['R1']}]
        self.assertEqual(wire_messages(messages), [{'role': 'user', 'content': 'x'}])
        plain = [{'role': 'user', 'content': 'x'}]
        self.assertIs(wire_messages(plain), plain)


# ----- agent-level ----------------------------------------------------------------------------------
class AgentRequestTests(Base):
    def test_single_ask_is_recorded_and_done(self):
        agent = self.agent(Script([text('hi')]))
        agent.ask('say hi')
        self.assertEqual(agent.requests.get('R1')['status'], 'done')
        done = self.of('done')[-1]
        self.assertEqual(done['open_items'], [])
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual(agent.messages[1]['relay_requests'], ['R1'])
        self.assertIn('update_todos', [t['function']['name'] for t in agent.tools()])
        self.assertTrue(self.of('requests'))

    def test_g1_step_limit_is_not_an_error(self):
        agent = self.agent(Script(default=forever_tools), max_steps=3)
        agent.ask('loop')
        done = self.events[-1]
        self.assertEqual((done['event'], done['stop_reason'], done['limit']['which'], done['limit']['steps']),
                         ('done', 'limit', 'steps', 3))
        self.assertEqual(done['open_items'][0]['id'], 'R1')
        self.assertEqual(agent.requests.get('R1')['status'], 'open')
        self.assertEqual(self.of('turn_summary')[-1]['stop_reason'], 'limit')
        self.assertIn('turn limit', agent.messages[-1]['content'])

    def test_g1_tool_call_cap(self):
        agent = self.agent(Script(default=lambda m: tools_msg(call('run_command', {'command': 'true'}),
                                                               call('run_command', {'command': 'true'}))),
                           max_tool_calls=3)
        agent.ask('many tools')
        done = self.events[-1]
        self.assertEqual((done['stop_reason'], done['limit']['which']), ('limit', 'tool_calls'))
        self.assertLessEqual(done['limit']['steps'], 3)

    def test_default_limits(self):
        # Uncapped by default since 2026-09-20 (card #2CZP): both defaults are validate_turn_options'
        # clamp maxima, so the settings are a backstop fuse rather than the working stop.
        agent = self.agent(Script())
        self.assertEqual((agent.max_steps, agent.max_tool_calls), (500, 2000))

    def test_completion_check_reprompts_at_most_twice(self):
        provider = Script([todos_call({'text': 'fix X', 'status': 'in_progress'}, {'text': 'rename Y', 'status': 'pending'})],
                          default=text('I am done.'))
        agent = self.agent(provider)
        agent.ask('fix X and also rename Y')
        checks = self.of('completion_check')
        self.assertEqual([c['reminder'] for c in checks], [1, 2])
        self.assertEqual(len(provider.requests), 4)   # todos, answer, answer after 2 reminders
        self.assertIn('Relay completion check 1/2', provider.requests[2][-1]['content'])
        done = self.events[-1]
        self.assertEqual(done['event'], 'done')
        self.assertEqual(sorted(i['id'] for i in done['open_items']), ['R1', 'T1', 'T2'])
        self.assertEqual(agent.requests.get('R1')['status'], 'open')

    def test_completion_check_resolved_by_update(self):
        provider = Script([todos_call({'text': 'fix X', 'status': 'in_progress'}, {'text': 'rename Y', 'status': 'pending'}),
                           text('done with X'),
                           todos_call({'id': 'T1', 'text': 'fix X', 'status': 'completed'},
                                      {'id': 'T2', 'text': 'rename Y', 'status': 'deferred', 'note': 'user must pick a name'}),
                           text('X fixed; Y deferred.')])
        agent = self.agent(provider)
        agent.ask('fix X and also rename Y')
        self.assertEqual(len(self.of('completion_check')), 1)
        self.assertEqual(self.events[-1]['open_items'], [])
        self.assertEqual((agent.requests.get('R1')['status'], agent.requests.get('R1')['reason']),
                         ('deferred', 'user must pick a name'))
        todos_events = self.of('todos')
        self.assertEqual([i['status'] for i in todos_events[-1]['items']], ['completed', 'deferred'])

    def test_completion_check_can_be_disabled(self):
        provider = Script([todos_call({'text': 'a', 'status': 'pending'})], default=text('bye'))
        agent = self.agent(provider, completion_check=False)
        agent.ask('a and b')
        self.assertFalse(self.of('completion_check'))

    def test_stale_todo_reminder(self):
        provider = Script([todos_call({'text': 'long job', 'status': 'in_progress'})] + [forever_tools] * 9,
                          default=text('finished'))
        agent = self.agent(provider, completion_check=False)
        agent.ask('long job')
        reminders = [m for m in agent.messages if 'update_todos has not been used for 8 steps' in (m.get('content') or '')]
        self.assertEqual(len(reminders), 1)
        self.assertEqual(reminders[0]['relay_kind'], 'note')

    def test_no_list_reminder_when_the_model_never_writes_one(self):
        """A turn doing real work with no todo list gets exactly one nudge (card D8VN).

        Nothing else covers this: the stale reminder needs open todos, the completion check counts a
        request as open only when an open todo points at it, and finish_turn marks a todo-less request
        done because the turn ended normally.
        """
        provider = Script([forever_tools] * 6, default=text('finished'))
        agent = self.agent(provider, completion_check=False)
        agent.ask('do five things')
        notes = [m for m in agent.messages if 'update_todos has not been used' in (m.get('content') or '')]
        self.assertEqual(len(notes), 1)
        self.assertIn('several parts', notes[0]['content'])
        self.assertEqual(notes[0]['relay_kind'], 'note')
        self.assertEqual(agent.todos.items, [])

    def test_no_list_reminder_is_silent_for_a_short_turn_or_once_a_list_exists(self):
        # A single simple ask that finishes inside NO_LIST_STEPS is never nudged.
        short = Script([forever_tools], default=text('done'))
        agent = self.agent(short, completion_check=False)
        agent.ask('read one file')
        self.assertEqual([m for m in agent.messages if 'update_todos has not been used' in (m.get('content') or '')], [])
        # A model that does write a list gets the stale reminder instead, never this one.
        provider = Script([todos_call({'text': 'long job', 'status': 'in_progress'})] + [forever_tools] * 9,
                          default=text('finished'))
        agent = self.agent(provider, completion_check=False)
        agent.ask('long job')
        notes = [m.get('content') or '' for m in agent.messages if 'update_todos has not been used' in (m.get('content') or '')]
        self.assertEqual(len(notes), 1)
        self.assertIn("for 8 steps", notes[0])          # the stale reminder
        self.assertNotIn('several parts', notes[0])     # not the no-list one

    def test_invalid_todo_update_returns_error_to_model(self):
        provider = Script([todos_call({'text': 'a', 'status': 'cancelled'}), text('ok')])
        agent = self.agent(provider)
        agent.ask('a')
        result = json.loads([m for m in agent.messages if m['role'] == 'tool'][0]['content'])
        self.assertIn('needs a note', result['error'])

    def test_todo_tool_can_be_turned_off(self):
        agent = self.agent(Script(), todo_tool=False)
        self.assertNotIn('update_todos', [t['function']['name'] for t in agent.tools()])
        self.assertNotIn('update_todos', agent.messages[0]['content'])

    def test_g5_compaction_after_three_steers_keeps_original_prompt(self):
        steers = [[{'prompt': f'steer {i}', 'ledger_id': None}] for i in range(3)]
        provider = Script([text('old answer')] + [forever_tools] * 3 + [text('all handled')])
        agent = self.agent(provider)
        agent.ask('an older turn')
        agent.steer_source = lambda: steers.pop(0) if steers else []
        original = 'ORIGINAL PROMPT: fix X, and also rename Y, oh and update the README'
        agent.ask(original)
        agent.steer_source = None
        agent.compact('manual')
        users = [m['content'] for m in agent.messages if m['role'] == 'user']
        self.assertIn(original, users)
        self.assertEqual(len([m for m in agent.messages if m.get('relay_kind') == 'steer']), 3)
        self.assertEqual([r['source'] for r in agent.requests.to_json()['items']], ['ask', 'ask', 'steer', 'steer', 'steer'])
        # Old behavior counted every user message as a turn start and summarized the prompt away.
        self.assertEqual(agent.messages[context.turn_starts(agent.messages)[-1]]['content'], original)

    def test_thirty_messages_compacted_twice_keep_every_request(self):
        provider = Script(side_reply='## Requests and intent\n(summary)')
        agent = self.agent(provider)
        for i in range(8):
            agent.ask(f'request number {i}: make file f{i}.txt')
        agent.compact('manual')
        for i in range(8, 15):
            agent.ask(f'request number {i}: make file f{i}.txt')
        event = agent.compact('manual')
        self.assertEqual(event['carried']['requests'], 15)
        text_ = json.dumps(agent.messages)
        for i in range(15):
            self.assertIn(f'request number {i}: make file f{i}.txt', text_)
        self.assertEqual(len([m for m in agent.messages if m.get('relay_kind') == 'carried']), 1)
        self.assertIn(context.MERGE_NOTE, provider.side_requests[-1][-1]['content'])
        self.assertNotIn(context.CARRIED_MARKER, provider.side_requests[-1][-1]['content'])
        # Rewind still works across the compactions (prefix-aware epoch shift).
        agent.rewind(15, 'conversation')
        self.assertEqual([m['content'] for m in agent.messages if m.get('relay_kind') == 'prompt'],
                         ['request number 13: make file f13.txt'])
        self.assertEqual(agent.messages[3]['relay_kind'], 'carried')

    def test_resume_restores_ledger_and_todos(self):
        provider = Script([todos_call({'text': 'a', 'status': 'completed'}, {'text': 'b', 'status': 'blocked', 'note': 'no key'}),
                           text('done')])
        agent = self.agent(provider)
        agent.ask('a and b')
        other = self.agent(Script())
        self.events.clear()
        other.resume(agent.session_id)
        self.assertEqual(other.requests.to_json()['items'], agent.requests.to_json()['items'])
        self.assertEqual(other.todos.items, agent.todos.items)
        listing = SessionStore(self.sessions).listing()
        self.assertEqual(listing[0]['open_requests'], 0)

    def test_legacy_session_backfills_ledger(self):
        agent = self.agent(Script())
        agent.ask('first')
        data = SessionStore(self.sessions).load(agent.session_id)
        for key in ('requests', 'todos'):
            data.pop(key)
        SessionStore(self.sessions).save(data)
        other = self.agent(Script())
        other.resume(agent.session_id)
        self.assertEqual([(i['id'], i['text'], i['status']) for i in other.requests.items], [('R1', 'first', 'done')])

    def test_rewind_restores_todos_and_truncates_ledger(self):
        provider = Script([todos_call({'text': 'one', 'status': 'completed'}), text('ok'),
                           todos_call({'id': 'T1', 'text': 'one', 'status': 'completed'}, {'text': 'two', 'status': 'completed'}),
                           text('ok')])
        agent = self.agent(provider)
        agent.ask('one')
        agent.ask('two')
        agent.rewind(2, 'conversation')
        self.assertEqual([t['text'] for t in agent.todos.items], ['one'])
        self.assertEqual([i['id'] for i in agent.requests.items], ['R1'])

    def test_fork_carries_ledger_up_to_turn(self):
        agent = self.agent(Script(), session_dir=None)
        for p in ('one', 'two', 'three'):
            agent.ask(p)
        state = agent.fork(2)
        self.assertEqual([i['text'] for i in state['requests']['items']], ['one', 'two'])
        other = self.agent(Script(), session_dir=None)
        other.load_state(state)
        self.assertEqual([(i['id'], i['turn']) for i in other.requests.items], [('R1', 1), ('R2', 2)])
        other.ask('four')
        self.assertEqual(other.requests.items[-1]['id'], 'R4')

    def test_audit_uses_router_model_and_only_flags(self):
        audit = Script(side_reply='{"unaddressed": [{"request_id": "R1", "quote": "update the README"}]}')
        audit.config = ProviderConfig('https://example.invalid/v1', 'router', 'k')
        self.router.side_effect = None
        self.router.return_value = audit
        provider = Script([text('fixed X')])
        agent = self.agent(provider, audit_requests=True)
        agent.ask('fix X and update the README')
        deadline = time.monotonic() + 5
        while not self.of('request_audit') and time.monotonic() < deadline:
            time.sleep(0.01)
        event = self.of('request_audit')[0]
        self.assertEqual(event['unaddressed'], [{'request_id': 'R1', 'quote': 'update the README'}])
        self.assertEqual(event['model'], route_assist.ROUTER_MODEL)
        self.assertEqual(audit.config.max_tokens, ledger_mod.AUDIT_MAX_TOKENS)
        self.assertIn('fix X and update the README', audit.side_requests[0][-1]['content'])
        self.assertEqual(agent.requests.get('R1')['audit'][0]['quote'], 'update the README')
        self.assertEqual(len(provider.requests), 1)   # never re-prompts

    def test_audit_falls_back_to_side_provider(self):
        self.router.side_effect = None
        self.router.return_value = None
        provider = Script([text('fixed')], side_reply='not json')
        agent = self.agent(provider, audit_requests=True)
        agent.ask('fix it')
        deadline = time.monotonic() + 5
        while not self.of('request_audit') and time.monotonic() < deadline:
            time.sleep(0.01)
        event = self.of('request_audit')[0]
        self.assertEqual((event['model'], event['unaddressed']), ('mock', []))
        self.assertIn('error', event)

    def test_audit_is_off_by_default(self):
        agent = self.agent(Script([text('x')]))
        agent.ask('y')
        time.sleep(0.05)
        self.assertFalse(self.of('request_audit'))
        self.router.assert_not_called()


# ----- queue-level drop paths --------------------------------------------------------------------------
class Blocking:
    """Tool-enabled calls block until released or cancelled; records every request's messages."""
    def __init__(self, first_tool=False):
        self.release = threading.Semaphore(0)
        self.seen = []
        self.first_tool = first_tool

    def complete(self, messages, tools, emit, cancel):
        self.seen.append(json.loads(json.dumps(messages)))
        emit({'event': 'delta', 'text': 'calls %d' % len(self.seen)})
        if self.first_tool and len(self.seen) == 1:
            return tools_msg(call('run_command', {'command': 'sleep 0.5'}, 'slow'))
        while not self.release.acquire(timeout=0.02):
            if cancel.is_set():
                raise Cancelled('Stopped.')
        return text('answer %d' % len(self.seen))

    def cancel(self):
        pass


class QueueDropPathTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.rec = Recorder()
        self.sup = TurnSupervisor(self.rec)

    def tearDown(self):
        self.sup.shutdown(timeout=3)
        self.temp.cleanup()

    def use(self, provider, **kw):
        self.agent = Agent(CONFIG(), self.temp.name, self.sup.agent_emit, provider=provider, **kw)
        self.sup.set_agent(self.agent)
        return provider

    def finished(self, item_id):
        return self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] == item_id)

    def test_g1_limit_does_not_pause_queue(self):
        self.use(Script(default=forever_tools), max_steps=2)
        first = self.sup.submit('loop forever', 'now')
        second = self.sup.submit('next prompt', 'queue')
        finished = self.finished(first)
        self.assertEqual((finished['outcome'], finished['stop_reason']), ('done', 'limit'))
        self.finished(second)
        self.assertFalse(any(e['paused'] for e in self.rec.of('queue_changed')))
        self.assertEqual(self.agent.requests.get('R1')['status'], 'open')

    def test_g2_interrupt_keeps_interrupted_request(self):
        p = self.use(Blocking())
        first = self.sup.submit('the interrupted request', 'now', request_id='g1')
        self.rec.wait(lambda e: e.get('text') == 'calls 1')
        queued = self.rec.wait(lambda e: e['event'] == 'queued' and e['id'] == first)
        self.assertEqual(queued['ledger_id'], 'R1')
        second = self.sup.submit('new urgent ask', 'interrupt')
        self.assertEqual(self.finished(first)['outcome'], 'cancelled')
        self.rec.wait(lambda e: e.get('text') == 'calls 2')
        p.release.release()
        self.finished(second)
        contents = [m['content'] for m in p.seen[1]]
        self.assertIn('the interrupted request', contents)
        self.assertTrue(any('not finished' in c for c in contents))
        self.assertTrue(contents[-1].endswith('new urgent ask'))
        self.assertEqual([(i['id'], i['source'], i['status']) for i in self.agent.requests.items],
                         [('R1', 'ask', 'open'), ('R2', 'interrupt', 'done')])

    def test_g2_cancel_then_resume_keeps_queued_items(self):
        p = self.use(Blocking())
        first = self.sup.submit('first', 'now')
        self.rec.wait(lambda e: e.get('text') == 'calls 1')
        queued = [self.sup.submit('q1', 'queue'), self.sup.submit('q2', 'queue')]
        self.sup.cancel()
        self.finished(first)
        time.sleep(0.1)
        self.assertEqual(len(p.seen), 1)   # paused
        self.assertEqual([i['status'] for i in self.agent.requests.items], ['open', 'open', 'open'])
        self.sup.resume()
        for item in queued:
            self.rec.wait(lambda e: e.get('text') == 'calls %d' % (len(p.seen)))
            p.release.release()
            self.finished(item)
        self.assertIn('first', [m['content'] for m in p.seen[1]])
        self.assertEqual([i['status'] for i in self.agent.requests.items], ['open', 'done', 'done'])

    def test_g3_steer_keeps_attachments_and_context(self):
        p = self.use(Blocking(first_tool=True))
        first = self.sup.submit('tool please', 'now')
        self.rec.wait(lambda e: e['event'] == 'tool_started')
        attachment = [{'path': '/tmp/notes.txt', 'kind': 'file', 'content': 'ATTACHED CONTENT', 'bytes': 16, 'truncated': False}]
        steer = self.sup.submit('also read my notes', 'steer', attachments=attachment,
                                context={'foreground_program': 'vim', 'terminal_cwd': '/tmp'})
        self.rec.wait(lambda e: e['event'] == 'steer_delivered' and e['ledger_ids'] == ['R2'])
        self.rec.wait(lambda e: e.get('text') == 'calls 2')
        p.release.release()
        self.finished(first)
        message = p.seen[1][-1]
        self.assertIn('[Sent by the user while you were working (R2,', message['content'])
        self.assertIn('Keep your current task (R1)', message['content'])
        self.assertIn('ATTACHED CONTENT', message['content'])
        self.assertIn('`vim`', message['content'])
        self.assertTrue(message['content'].endswith('also read my notes'))
        self.assertEqual(message['relay_kind'], 'steer')
        r2 = self.agent.requests.get('R2')
        self.assertEqual((r2['source'], r2['status'], r2['handled'], r2['attachments']), ('steer', 'done', True, ['/tmp/notes.txt']))
        self.assertEqual(self.agent.requests.get('R1')['status'], 'done')
        self.assertFalse(self.rec.of('steer_returned'))
        self.assertIsNotNone(steer)

    def test_steer_returned_requeues_exactly_once(self):
        p = self.use(Blocking())
        first = self.sup.submit('first', 'now')
        self.rec.wait(lambda e: e.get('text') == 'calls 1')
        steer = self.sup.submit('late thought', 'steer')
        p.release.release()
        returned = self.rec.wait(lambda e: e['event'] == 'steer_returned')
        self.assertEqual((returned['ledger_id'], returned['requeued']), ('R2', True))
        self.finished(first)
        self.rec.wait(lambda e: e.get('text') == 'calls 2')
        p.release.release()
        self.finished(steer)
        time.sleep(0.1)
        self.assertEqual(len(p.seen), 2)
        self.assertEqual(len(self.agent.requests.items), 2)
        self.assertEqual(len(self.rec.of('agent_started')), 2)
        self.assertEqual(self.agent.requests.get('R2')['status'], 'done')

    def test_remove_and_clear_mark_cancelled_by_user(self):
        p = self.use(Blocking())
        first = self.sup.submit('first', 'now')
        self.rec.wait(lambda e: e.get('text') == 'calls 1')
        a = self.sup.submit('a', 'queue')
        self.sup.submit('b', 'queue')
        self.sup.remove(a)
        self.sup.clear()
        p.release.release()
        self.finished(first)
        self.assertEqual([(i['id'], i['status']) for i in self.agent.requests.items],
                         [('R1', 'done'), ('R2', 'cancelled_by_user'), ('R3', 'cancelled_by_user')])


class ProtocolCommandTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.rec = Recorder()
        self.sup = TurnSupervisor(self.rec)
        self.agent = Agent(CONFIG(), self.temp.name, self.sup.agent_emit, provider=Script(default=text('ok')))
        self.sup.set_agent(self.agent)
        self.commands = SessionCommands(self.sup, self.rec)

    def tearDown(self):
        self.sup.shutdown(timeout=3)
        self.temp.cleanup()

    def test_list_get_set_reask_todos(self):
        first = self.sup.submit('please do the thing', 'now')
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] == first)
        self.commands.handle('requests', {'type': 'requests', 'id': 'q1'})
        listing = self.rec.wait(lambda e: e['event'] == 'requests' and e.get('id') == 'q1')
        self.assertEqual((listing['total'], listing['open'], listing['items'][0]['status']), (1, 0, 'done'))
        self.assertNotIn('text', listing['items'][0])
        self.commands.handle('request_get', {'type': 'request_get', 'id': 'q2', 'ledger_id': 'R1'})
        got = self.rec.wait(lambda e: e['event'] == 'request' and e.get('id') == 'q2')
        self.assertEqual(got['item']['text'], 'please do the thing')
        self.commands.handle('request_set', {'type': 'request_set', 'ledger_id': 'R1', 'status': 'open'})
        self.assertEqual(self.agent.requests.get('R1')['status'], 'open')
        with self.assertRaises(ValueError):
            self.commands.handle('request_set', {'type': 'request_set', 'ledger_id': 'R1', 'status': 'blocked'})
        self.commands.handle('request_reask', {'type': 'request_reask', 'id': 'q3', 'ledger_id': 'R1', 'when': 'queue'})
        queued = self.rec.wait(lambda e: e['event'] == 'queued' and e.get('request_id') == 'q3')
        self.assertEqual(queued['ledger_id'], 'R1')
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] == queued['id'])
        item = self.agent.requests.get('R1')
        self.assertEqual((item['status'], item['reasked'], len(self.agent.requests.items)), ('done', 1, 1))
        self.commands.handle('todos', {'type': 'todos', 'id': 'q4'})
        self.assertEqual(self.rec.wait(lambda e: e['event'] == 'todos' and e.get('id') == 'q4')['items'], [])

    def test_set_options(self):
        self.assertEqual(self.agent.set_options({'max_steps': 80, 'audit_requests': True})['max_steps'], 80)
        self.assertTrue(self.agent.audit_requests)
        with self.assertRaises(ValueError):
            self.agent.set_options({'max_tool_calls': 0})


if __name__ == '__main__':
    unittest.main()
