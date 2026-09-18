"""Todos mapped to subagents (card #QHR1): a todo handed to a subagent follows it, both ways.

Uses the fake providers of test_subagents: no network."""
import json
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path

from relay_core import todos as todo_mod
from test_subagents import Base, call, calls, final


def todos(*items):
    return calls(call('update_todos', {'items': list(items)}, f'u{len(items)}{id(items) % 1000}'))


def last_todos(rec):
    return {t['id']: t for t in rec.of('todos')[-1]['items']}


class ValidateTests(unittest.TestCase):
    def test_delegated_todo_keeps_status_and_link_and_does_not_count(self):
        existing = [{'id': 'T1', 'text': 'a', 'status': 'in_progress', 'request_ids': ['R1'], 'note': None,
                     'subagent': 'a1'},
                    {'id': 'T2', 'text': 'b', 'status': 'pending', 'request_ids': ['R1'], 'note': None,
                     'subagent': None}]
        # The model resends T1 as pending (and echoes the read-only field): T1 keeps its subagent's status.
        items, _ = todo_mod.validate({'items': [
            {'id': 'T1', 'text': 'a', 'status': 'pending', 'subagent': 'a9'},
            {'id': 'T2', 'text': 'b', 'status': 'in_progress'}]}, {'R1'}, existing, 3, delegated=frozenset({'T1'}))
        self.assertEqual(items[0]['status'], 'in_progress')
        self.assertEqual(items[0]['subagent'], 'a1')
        self.assertEqual(items[1]['status'], 'in_progress')
        self.assertIsNone(items[1]['subagent'])
        # Without a delegation several in_progress todos are fine too (no one-in_progress rule).
        items, _ = todo_mod.validate({'items': [{'id': 'T1', 'text': 'a', 'status': 'in_progress'},
                                                {'id': 'T2', 'text': 'b', 'status': 'in_progress'}]},
                                     {'R1'}, existing, 3)
        self.assertEqual([t['status'] for t in items], ['in_progress', 'in_progress'])

    def test_finish_outcomes(self):
        for outcome, status, note in (('done', 'completed', None), ('failed', 'blocked', 'Subagent a1 failed: boom'),
                                      ('stopped', 'pending', 'Subagent a1 was stopped before it finished.')):
            todo_list = todo_mod.TodoList()
            todo_list.replace({'items': [{'text': 'a', 'status': 'pending'}]}, set(), None)
            self.assertTrue(todo_list.subagent_started('T1', 'a1'))
            self.assertEqual(todo_list.open_items(include_delegated=False), [])
            self.assertTrue(todo_list.event()['items'][0]['subagent_running'])
            self.assertTrue(todo_list.subagent_finished('a1', outcome, 'boom'))
            todo = todo_list.items[0]
            self.assertEqual((todo['status'], todo['note'], todo['subagent']), (status, note, 'a1'))
            self.assertFalse(todo_list.event()['items'][0]['subagent_running'])
            self.assertFalse(todo_list.subagent_finished('a1', outcome))   # only once

    def test_check_delegable(self):
        todo_list = todo_mod.TodoList()
        todo_list.replace({'items': [{'text': 'a', 'status': 'pending'},
                                     {'text': 'b', 'status': 'completed'},
                                     {'text': 'c', 'status': 'blocked', 'note': 'failed once'},
                                     {'text': 'd', 'status': 'cancelled', 'note': 'not wanted'}]}, set(), None)
        self.assertEqual(todo_list.check_delegable('T1')['text'], 'a')
        self.assertEqual(todo_list.check_delegable('T3')['text'], 'c')   # a retry after a failure
        for bad, words in (('T2', 'completed'), ('T4', 'cancelled'), ('T9', 'No todo'), ('x', 'todo id')):
            with self.assertRaises(ValueError) as caught:
                todo_list.check_delegable(bad)
            self.assertIn(words, str(caught.exception))
        todo_list.subagent_started('T1', 'a1')
        with self.assertRaises(ValueError) as caught:
            todo_list.check_delegable('T1')
        self.assertIn('already being worked on by subagent a1', str(caught.exception))
        # A different subagent resuming does not take a todo another one runs.
        self.assertFalse(todo_list.subagent_started('T1', 'a2'))

    def test_saved_link_survives_load_and_rewind_keeps_running_link(self):
        todo_list = todo_mod.TodoList()
        todo_list.replace({'items': [{'text': 'a', 'status': 'pending'}, {'text': 'b', 'status': 'pending'}]},
                          set(), None)
        before = todo_list.snapshot()
        todo_list.subagent_started('T1', 'a1')
        saved = json.loads(json.dumps(todo_list.to_json()))
        loaded = todo_mod.TodoList()
        loaded.load_json(saved)
        self.assertEqual(loaded.items[0]['status'], 'pending')
        self.assertEqual(loaded.items[0]['subagent'], 'a1')
        self.assertIn('not running any more', loaded.items[0]['note'])
        self.assertEqual(loaded.delegated, {})
        # Rewind to a list from before the link: the running subagent's todo is not re-linked by magic.
        todo_list.restore(before)
        self.assertEqual(todo_list.delegated, {})
        # Rewind to a list that has the link: it stays live and in_progress.
        todo_list.subagent_started('T1', 'a1')
        linked = todo_list.snapshot()
        todo_list.restore(linked)
        self.assertEqual(todo_list.delegated, {'T1': 'a1'})
        self.assertEqual(todo_list.items[0]['status'], 'in_progress')
        self.assertIsNone(todo_list.items[0]['note'])


class ModelLinkTests(Base):
    def test_background_subagent_with_todo_id_drives_the_todo_and_the_request(self):
        agent, provider = self.main_agent([
            todos({'text': 'write docs', 'status': 'pending'}, {'text': 'fix bug', 'status': 'pending'}),
            calls(call('agent', {'description': 'docs', 'prompt': 'D gate:g1', 'subagent_type': 'general',
                                 'background': True, 'todo_id': 'T1'}, 'c1')),
            # T2 in_progress beside T1, which a subagent runs.
            todos({'id': 'T1', 'text': 'write docs', 'status': 'in_progress'},
                  {'id': 'T2', 'text': 'fix bug', 'status': 'in_progress'}),
            todos({'id': 'T1', 'text': 'write docs', 'status': 'pending'},
                  {'id': 'T2', 'text': 'fix bug', 'status': 'completed'}),
            final('T2 done, T1 is with a1')])
        agent.ask('write docs and fix bug')
        started = self.rec.of('subagent_started')[0]
        self.assertEqual(started['todo_id'], 'T1')
        results = self.tool_results(provider)
        self.assertEqual(results['c1']['todo_id'], 'T1')
        self.assertTrue(all('error' not in r for r in results.values()), results)
        # The update_todos result shows the model which todo a subagent has.
        update = [r for cid, r in results.items() if cid.startswith('u')]
        self.assertTrue(any(i['subagent'] == 'a1' and i['subagent_running'] for r in update for i in r['items']))
        todo = last_todos(self.rec)['T1']
        self.assertEqual((todo['status'], todo['subagent'], todo['subagent_running']), ('in_progress', 'a1', True))
        # The completion check did not nag about T1 (a subagent has it) and the turn ended.
        self.assertFalse([e for e in self.rec.of('completion_check')])
        self.assertEqual(self.rec.of('done')[-1]['event'], 'done')
        self.assertEqual(agent.requests.get('R1')['status'], 'open')   # T1 is still being worked
        self.assertEqual(self.manager.list()[0]['todo_id'], 'T1')
        self.hub.open('g1')
        self.rec.wait(lambda e: e['event'] == 'todos' and e['items'][0]['status'] == 'completed')
        todo = last_todos(self.rec)['T1']
        self.assertEqual((todo['status'], todo['subagent'], todo['subagent_running']), ('completed', 'a1', False))
        self.assertEqual(agent.requests.get('R1')['status'], 'done')
        # The result handed to the main agent says what happened to the todo.
        note = self.manager._pending['a1']['note']
        self.assertIn('It worked on todo T1; Relay has marked that todo completed.', note)

    def test_stopped_subagent_puts_the_todo_back(self):
        agent, provider = self.main_agent([
            todos({'text': 'long job', 'status': 'pending'}),
            calls(call('agent', {'description': 'job', 'prompt': 'J gate:g1', 'subagent_type': 'general',
                                 'background': True, 'todo_id': 'T1'}, 'c1')),
            final('started')])
        agent.ask('long job')
        self.manager.stop('a1')
        self.rec.wait(lambda e: e['event'] == 'subagent_finished' and e['id'] == 'a1')
        todo = last_todos(self.rec)['T1']
        self.assertEqual(todo['status'], 'pending')
        self.assertIn('was stopped', todo['note'])

    def test_bad_todo_ids_are_refused(self):
        agent, provider = self.main_agent([
            todos({'text': 'done already', 'status': 'completed'}),
            calls(call('agent', {'description': 'x', 'prompt': 'X', 'subagent_type': 'general', 'todo_id': 'T1'}, 'c1'),
                  call('agent', {'description': 'y', 'prompt': 'Y', 'subagent_type': 'general', 'todo_id': 'T7'}, 'c2')),
            final()])
        agent.ask('go')
        results = self.tool_results(provider)
        self.assertIn('completed', results['c1']['error'])
        self.assertIn('No todo T7', results['c2']['error'])
        self.assertEqual(self.rec.of('subagent_started'), [])

    def test_failed_foreground_subagent_blocks_its_todo(self):
        class Failing:
            def cancel(self):
                pass

            def complete(self, messages, tools, emit, cancel):
                raise ValueError('provider said no')
        self.manager.factory.provider_factory = lambda config: Failing()
        agent, provider = self.main_agent([
            todos({'text': 'risky', 'status': 'pending'}),
            calls(call('agent', {'description': 'r', 'prompt': 'R', 'subagent_type': 'general', 'todo_id': 'T1'}, 'c1')),
            todos({'id': 'T1', 'text': 'risky', 'status': 'blocked', 'note': 'subagent failed'}),
            final()])
        agent.ask('risky')
        todo = next(e for e in self.rec.of('todos') if e['items'][0]['status'] == 'blocked')['items'][0]
        self.assertEqual(todo['subagent'], 'a1')
        self.assertIn('Subagent a1 failed', todo['note'])


class UserHandoffTests(Base):
    def test_todo_subagent_task_carries_the_request_and_tells_the_main_agent(self):
        agent, provider = self.main_agent([
            todos({'text': 'rename the flag', 'status': 'pending'}, {'text': 'update docs', 'status': 'pending'}),
            final('listed')])
        agent.ask('please rename the --fast flag to --flash, then update docs')
        args = agent.todo_subagent_task('T1')
        self.assertEqual(args['background'], True)
        self.assertEqual(args['description'], 'rename the flag')
        self.assertIn('todo T1', args['prompt'])
        self.assertIn('please rename the --fast flag to --flash, then update docs', args['prompt'])
        sub = self.manager.spawn({**args, 'todo_id': 'T1'})
        self.manager.notify_main(f'The user handed todo T1 to background subagent {sub.id} from the task list.')
        self.assertEqual(last_todos(self.rec)['T1']['subagent'], 'a1')
        self.rec.wait(lambda e: e['event'] == 'todos' and e['items'][0]['status'] == 'completed')
        self.assertIn('rename the flag', self.hub.seen[0][0])
        # The next main model call reads the notice (and the delivered result).
        provider.script.append(final('ok'))
        agent.ask('what now?')
        seen = json.dumps(provider.seen[-1][0])
        self.assertIn('The user handed todo T1 to background subagent a1', seen)
        with self.assertRaises(ValueError):
            agent.todo_subagent_task('T1')   # completed now


class WorkerTodoSubagentTests(unittest.TestCase):
    def test_todo_subagent_command_errors_cleanly(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temp:
            payload = ''.join(json.dumps(m) + '\n' for m in [
                {'type': 'todo_subagent', 'id': 'q0', 'todo_id': 'T1'},
                {'type': 'configure', 'base_url': 'http://127.0.0.1:1/v1', 'model': 't', 'api_key': '',
                 'workspace': temp, 'agents': {'dirs': []}},
                {'type': 'todo_subagent', 'id': 'q1', 'todo_id': 'T1'},
                {'type': 'shutdown'}])
            proc = subprocess.run([sys.executable, '-S', str(root / 'backend/worker.py')], input=payload, text=True,
                                  capture_output=True, timeout=10, cwd=root)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            events = [json.loads(line) for line in proc.stdout.splitlines()]
            errors = {e.get('id'): e['text'] for e in events if e['event'] == 'error'}
            self.assertIn('Configure', errors['q0'])
            self.assertIn('No todo T1', errors['q1'])


if __name__ == '__main__':
    unittest.main()
