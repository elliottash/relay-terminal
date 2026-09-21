"""Agent sessions: model/effort switching, context and compaction, checkpoints, rewind, fork,
sessions and recaps, plan mode, attachments. Fake providers only; no network."""
import builtins
import json
import os
import re
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

from relay_core import agent as agent_module
from relay_core import attachments, checkpoints, planning, sessions as session_files, suggestions
from relay_core.agent import Agent
from relay_core.context import SUMMARY_MARKER, ContextTracker, limit_tokens
from relay_core.provider import ProviderConfig
from relay_core.sessions import SessionStore

CONFIG = lambda: ProviderConfig('http://127.0.0.1:12345/v1', 'mock', '')  # noqa: E731


def call(name, arguments, call_id='call-1'):
    return {'id': call_id, 'type': 'function', 'function': {'name': name, 'arguments': json.dumps(arguments)}}


class ScriptedProvider:
    """Returns scripted responses for tool-enabled calls; answers no-tools calls with `side_reply`."""
    def __init__(self, responses=(), side_reply='SUMMARY TEXT', usage=None, name='A'):
        self.responses = list(responses)
        self.side_reply = side_reply
        self.usage = usage
        self.name = name
        self.requests = []      # (messages copy, tool names)
        self.side_requests = []

    def complete(self, messages, tools, emit, cancel):
        snapshot = json.loads(json.dumps(messages))
        if not tools:
            self.side_requests.append(snapshot)
            return {'role': 'assistant', 'content': self.side_reply}
        self.requests.append((snapshot, [t['function']['name'] for t in tools]))
        if self.usage is not None:
            emit({'event': 'usage', 'usage': self.usage})
        if self.responses:
            response = self.responses.pop(0)
            return response(snapshot) if callable(response) else response
        return {'role': 'assistant', 'content': f'reply from {self.name}'}

    def cancel(self):
        pass


def text(content):
    return {'role': 'assistant', 'content': content}


def tools_msg(*calls):
    return {'role': 'assistant', 'content': '', 'reasoning_content': 'thinking', 'tool_calls': list(calls)}


class Base(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name) / 'ws'
        self.root.mkdir()
        self.sessions = Path(self.temp.name) / 'sessions'
        self.events = []

    def tearDown(self):
        self.temp.cleanup()

    def agent(self, provider, **kwargs):
        kwargs.setdefault('session_dir', str(self.sessions))
        return Agent(CONFIG(), str(self.root), self.events.append, provider=provider, **kwargs)

    def of(self, kind):
        return [e for e in self.events if e['event'] == kind]


class ModelAndEffortTests(Base):
    def test_set_model_keeps_conversation(self):
        first = ScriptedProvider([text('The codeword is PLUM.')], name='A')
        agent = self.agent(first, preset_id='kimi')
        agent.ask('Remember the codeword PLUM.')
        second = ScriptedProvider(name='B')
        agent.set_model(ProviderConfig('https://openrouter.ai/api/v1', 'deepseek/deepseek-v4.1-flash', 'k'),
                        'openrouter', provider=second)
        agent.ask('What was the codeword?')
        sent = second.requests[0][0]
        self.assertTrue(any('PLUM' in (m.get('content') or '') for m in sent if m['role'] == 'assistant'))
        self.assertEqual(sent[-1]['content'], 'What was the codeword?')
        self.assertEqual(agent.context.window, 1_048_576)

    def test_effort_applied_per_provider_style(self):
        agent = Agent(ProviderConfig('https://api.moonshot.ai/v1', 'kimi-k3', 'k', {'reasoning_effort': 'high'}),
                      str(self.root), self.events.append, provider=ScriptedProvider(), preset_id='kimi')
        self.assertEqual(agent.effort, 'high')  # inferred from the preset's extra
        self.assertEqual(agent.set_effort('max'), {'reasoning_effort': 'max'})
        self.assertEqual(agent.config.extra['reasoning_effort'], 'max')
        agent.set_model(ProviderConfig('https://api.z.ai/api/coding/paas/v4', 'glm-5.3', 'k'), 'glm-coding')
        self.assertEqual(agent.config.extra, {'thinking': {'type': 'enabled'}, 'reasoning_effort': 'max'})
        agent.set_model(ProviderConfig('https://openrouter.ai/api/v1', 'deepseek/deepseek-v4.1-flash', 'k'), 'openrouter')
        self.assertEqual(agent.config.extra, {'reasoning': {'effort': 'xhigh'}})
        with self.assertRaises(ValueError):
            agent.set_effort('extreme')

    def test_history_adapted_for_kimi_after_openrouter(self):
        provider = ScriptedProvider([
            {'role': 'assistant', 'content': '', 'reasoning': 'or-thoughts',
             'tool_calls': [call('list_directory', {'path': '.'})]},
            text('done')])
        agent = self.agent(provider, preset_id='openrouter')
        agent.ask('list')
        agent.set_model(ProviderConfig('https://api.moonshot.ai/v1', 'kimi-k3', 'k'), 'kimi')
        assistant = [m for m in agent.messages if m.get('tool_calls')][0]
        self.assertEqual(assistant['reasoning_content'], 'or-thoughts')


class ContextTests(Base):
    def test_context_event_uses_usage_or_estimate(self):
        agent = self.agent(ScriptedProvider(usage={'prompt_tokens': 1000, 'completion_tokens': 50, 'total_tokens': 1050}))
        agent.ask('hello')
        event = self.of('context')[-1]
        self.assertEqual(event['used_tokens'], 1050)
        self.assertFalse(event['estimated'])
        self.assertEqual(event['window'], 128_000)
        agent2 = self.agent(ScriptedProvider())
        agent2.ask('hello')
        self.assertTrue(self.of('context')[-1]['estimated'])

    def test_limit_formula(self):
        self.assertEqual(limit_tokens(1_048_576, 0.80, 32768), 838_860)
        self.assertEqual(limit_tokens(200_000, 0.95, 32768), 200_000 - 32768 - 24000)
        self.assertEqual(ContextTracker(8000, 0.8, 8192).limit, 6400)  # tiny window falls back to the fraction

    def test_auto_compaction_summarizes_and_keeps_tool_groups_intact(self):
        big = 'x' * 6000
        responses = []
        for i in range(3):
            responses += [tools_msg(call('run_command', {'command': f'printf {big[:10]}; head -c 5000 /dev/zero | tr "\\0" y'}, f'c{i}')),
                          text(f'turn {i} done')]
        provider = ScriptedProvider(responses, side_reply='## Objective\nkeep going')
        agent = self.agent(provider, context_window=8000, compact_threshold=0.5)
        for i in range(3):
            agent.ask(f'task {i} ' + 'y' * 800)
        started = self.of('compaction_started')
        self.assertTrue(started)
        self.assertEqual(started[0]['reason'], 'auto')
        # Auto-compaction fires once per turn while the conversation is over the limit, and a round
        # that finds nothing left it can compact reports before == after (the first round here does,
        # on main as well). Which round lands last therefore moves with the size of the tool list —
        # `ask_user` (#MQ9C) was enough to flip it — so what is asserted is that compaction reduced
        # the conversation, not that the last of several rounds happened to be a reducing one.
        compactions = self.of('compacted')
        self.assertTrue(any(c['after_tokens'] < c['before_tokens'] for c in compactions),
                        [(c['before_tokens'], c['after_tokens']) for c in compactions])
        # every tool message directly follows its assistant tool-call group
        for messages, _ in provider.requests:
            for i, m in enumerate(messages):
                if m['role'] == 'tool':
                    j = i - 1
                    while messages[j]['role'] == 'tool':
                        j -= 1
                    ids = [c['id'] for c in messages[j].get('tool_calls', [])]
                    self.assertIn(m['tool_call_id'], ids)
        self.assertTrue(provider.side_requests, 'summary call expected')
        self.assertTrue(any(SUMMARY_MARKER in (m.get('content') or '') for m in agent.messages))

    def test_manual_compact_with_focus(self):
        provider = ScriptedProvider(side_reply='summary of old turns')
        agent = self.agent(provider)
        for i in range(4):
            agent.ask(f'prompt {i}')
        agent.compact('manual', 'the parser work')
        self.assertIn('the parser work', provider.side_requests[-1][-1]['content'])
        self.assertEqual(provider.side_requests[-1][-1]['content'].count('### USER'), 2)
        self.assertEqual(agent.messages[1]['content'], SUMMARY_MARKER + '\n\nsummary of old turns')
        self.assertEqual([m['content'] for m in agent.messages if m['role'] == 'user'][-2:], ['prompt 2', 'prompt 3'])
        event = self.of('compacted')[-1]
        self.assertEqual(event['summary_chars'], len('summary of old turns'))


class CheckpointTests(Base):
    def write_turns(self):
        (self.root / 'a.txt').write_text('original\n')
        provider = ScriptedProvider([
            tools_msg(call('write_file', {'path': 'a.txt', 'content': 'turn1\n'})), text('ok'),
            tools_msg(call('write_file', {'path': 'a.txt', 'content': 'turn2\n'}),
                      call('write_file', {'path': 'new.txt', 'content': 'created\n'}, 'call-2')), text('ok')])
        agent = self.agent(provider)
        agent.ask('first edit')
        agent.ask('second edit')
        return agent

    def test_checkpoints_list_files_per_turn(self):
        agent = self.write_turns()
        items = agent.checkpoint_listing()
        self.assertEqual([i['turn'] for i in items], [1, 2])
        self.assertEqual(items[0]['files'], [str(self.root / 'a.txt')])
        self.assertEqual(sorted(items[1]['files']), sorted([str(self.root / 'a.txt'), str(self.root / 'new.txt')]))
        self.assertTrue(items[0]['conversation'])

    def test_rewind_undoes_an_edit_file(self):
        (self.root / 'a.txt').write_text('alpha\nbeta\n')
        provider = ScriptedProvider([
            tools_msg(call('edit_file', {'path': 'a.txt', 'old_string': 'beta', 'new_string': 'BETA'})), text('ok')])
        agent = self.agent(provider)
        agent.ask('edit it')
        self.assertEqual((self.root / 'a.txt').read_text(), 'alpha\nBETA\n')
        self.assertEqual(agent.checkpoint_listing()[0]['files'], [str(self.root / 'a.txt')])
        agent.rewind(1, 'files')
        self.assertEqual((self.root / 'a.txt').read_text(), 'alpha\nbeta\n')

    def test_rewind_files_both_turns(self):
        agent = self.write_turns()
        event = agent.rewind(1, 'files')
        self.assertEqual((self.root / 'a.txt').read_text(), 'original\n')
        self.assertFalse((self.root / 'new.txt').exists())
        self.assertEqual(event['conflicts'], [])
        self.assertIn('never undone', event['note'])
        self.assertEqual(len([m for m in agent.messages if m['role'] == 'user']), 2)  # conversation kept

    def test_conflict_is_skipped(self):
        agent = self.write_turns()
        (self.root / 'a.txt').write_text('user edit\n')
        event = agent.rewind(2, 'files')
        self.assertEqual(event['conflicts'], [str(self.root / 'a.txt')])
        self.assertEqual((self.root / 'a.txt').read_text(), 'user edit\n')
        self.assertFalse((self.root / 'new.txt').exists())

    def test_rewind_conversation_truncates_and_notes_next_turn(self):
        agent = self.write_turns()
        event = agent.rewind(2, 'conversation')
        self.assertEqual(event['prompt'], 'second edit')
        self.assertEqual((self.root / 'a.txt').read_text(), 'turn2\n')  # files untouched
        self.assertEqual([m['content'] for m in agent.messages if m['role'] == 'user'], ['first edit'])
        self.assertEqual([i['turn'] for i in agent.checkpoint_listing()], [1])
        provider = agent.provider
        agent.ask('again')
        self.assertIn('rewound', provider.requests[-1][0][-1]['content'])
        self.assertTrue(provider.requests[-1][0][-1]['content'].endswith('again'))
        self.assertEqual(agent.checkpoint_listing()[-1]['turn'], 3)

    def test_rewind_conversation_after_compaction_uses_snapshot(self):
        provider = ScriptedProvider(side_reply='sum')
        agent = self.agent(provider)
        for i in range(4):
            agent.ask(f'prompt {i}')
        agent.compact('manual')
        self.assertEqual(agent.epoch, 1)
        agent.rewind(2, 'conversation')
        self.assertEqual([m['content'] for m in agent.messages if m['role'] == 'user'], ['prompt 0'])
        self.assertEqual(agent.epoch, 0)
        # a kept turn is still rewindable in the new epoch too
        agent2 = self.agent(ScriptedProvider(side_reply='sum'))
        for i in range(4):
            agent2.ask(f'p{i}')
        agent2.compact('manual')
        agent2.rewind(4, 'conversation')
        self.assertTrue(agent2.messages[1]['content'].startswith(SUMMARY_MARKER))
        self.assertEqual([m['content'] for m in agent2.messages if m['role'] == 'user'][-1], 'p2')

    def test_rewind_keeps_what_it_undid(self):
        """The turns a rewind drops are written out first, verbatim (card #0TJ9)."""
        agent = self.write_turns()
        before = [dict(m) for m in agent.messages]
        event = agent.rewind(2, 'conversation')
        self.assertEqual(event['rewound_n'], 1)
        records = SessionStore(self.sessions).rewound(agent.session_id)
        self.assertEqual(len(records), 1)
        record = records[0]
        self.assertEqual(sorted(record), sorted(['n', 'at', 'turn', 'restore', 'epoch', 'prompt',
                                                 'messages', 'restored_files', 'conflicts']))
        self.assertEqual((record['n'], record['turn'], record['restore'], record['epoch']),
                         (1, 2, 'conversation', 0))
        self.assertEqual(record['prompt'], 'second edit')
        self.assertEqual((record['restored_files'], record['conflicts']), ([], []))
        self.assertLess(abs(record['at'] - time.time()), 60)
        # nothing is lost: what is left plus what was kept is the conversation as it was
        self.assertEqual(agent.messages + record['messages'], before)
        self.assertEqual(record['messages'][0]['content'], 'second edit')
        self.assertEqual(oct(os.stat(self.sessions / f'{agent.session_id}.rewound.jsonl').st_mode & 0o777),
                         '0o600')

    def test_rewind_after_compaction_keeps_the_replaced_conversation(self):
        agent = self.agent(ScriptedProvider(side_reply='sum'))
        for i in range(4):
            agent.ask(f'prompt {i}')
        agent.compact('manual')
        before = [dict(m) for m in agent.messages]
        agent.rewind(2, 'conversation')
        record = SessionStore(self.sessions).rewound(agent.session_id)[0]
        self.assertEqual(record['epoch'], 1)                 # the epoch it was rewound *from*
        # the compacted conversation is replaced wholesale, so everything past the system message
        # is what left the pane
        self.assertEqual(agent.messages[:1] + record['messages'], before)
        self.assertTrue(any(m.get('content') == 'prompt 3' for m in record['messages']))

    def test_rewound_records_count_up_and_the_oldest_drop_off(self):
        agent = self.agent(ScriptedProvider(side_reply='sum'))
        store = SessionStore(self.sessions)
        for expected in range(1, 23):
            agent.ask('again')
            self.assertEqual(agent.rewind(agent.checkpoint_listing()[-1]['turn'], 'conversation')['rewound_n'],
                             expected)
        records = store.rewound(agent.session_id)
        self.assertEqual([r['n'] for r in records], list(range(3, 23)))   # newest 20, numbers kept

    def test_rewind_files_keeps_no_record(self):
        agent = self.write_turns()
        event = agent.rewind(1, 'files')
        self.assertIsNone(event['rewound_n'])
        self.assertFalse((self.sessions / f'{agent.session_id}.rewound.jsonl').exists())

    def test_a_failed_sidecar_write_does_not_fail_the_rewind(self):
        agent = self.write_turns()

        def refuse(*args, **kwargs):
            raise OSError('no room')

        agent.store.append_rewound = refuse
        event = agent.rewind(2, 'conversation')
        self.assertIsNone(event['rewound_n'])
        self.assertEqual([m['content'] for m in agent.messages if m['role'] == 'user'], ['first edit'])

    def test_rewound_file_survives_a_truncated_last_line(self):
        agent = self.write_turns()
        agent.rewind(2, 'conversation')
        path = self.sessions / f'{agent.session_id}.rewound.jsonl'
        with open(path, 'a', encoding='utf-8') as handle:
            handle.write('{"n": 2, "turn": 1, "mess')     # a crash mid-append
        store = SessionStore(self.sessions)
        self.assertEqual([r['n'] for r in store.rewound(agent.session_id)], [1])
        self.assertEqual(store.append_rewound(agent.session_id, {'turn': 1}), 2)

    def test_rewind_unknown_turn(self):
        agent = self.agent(ScriptedProvider())
        with self.assertRaises(ValueError):
            agent.rewind(9, 'both')
        with self.assertRaises(ValueError):
            agent.rewind(1, 'everything')


class SessionTests(Base):
    def test_autosave_list_resume(self):
        agent = self.agent(ScriptedProvider())
        agent.ask('Fix the login bug')
        agent.ask('Now add a test')
        items = SessionStore(self.sessions).listing()
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0]['title'], 'Fix the login bug')
        self.assertEqual(items[0]['turns'], 2)
        self.assertEqual(oct(os.stat(self.sessions / f"{agent.session_id}.json").st_mode & 0o777), '0o600')
        other = self.agent(ScriptedProvider(name='B'))
        event = other.resume(agent.session_id)
        self.assertEqual(event['turns'], 2)
        self.assertEqual(other.session_id, agent.session_id)
        self.assertEqual(other.messages[1:], agent.messages[1:])
        other.ask('third')
        self.assertEqual(SessionStore(self.sessions).listing()[0]['turns'], 3)

    def test_resume_reports_a_turn_left_open(self):
        """`state_loaded` carries `turn_open` so a restored pane may offer Continue (#SXF1).

        A turn Relay was killed mid-flight is in the session file with its last checkpoint
        unfinished; one that ended — or a session from before the `ended` field — is not.
        """
        agent = self.agent(ScriptedProvider())
        agent.ask('Fix the login bug')
        agent.ask('Now add a test')
        path = self.sessions / f"{agent.session_id}.json"
        self.assertFalse(self.agent(ScriptedProvider(name='B')).resume(agent.session_id)['turn_open'])
        saved = json.loads(path.read_text())
        saved['checkpoints']['items'][-1].pop('ended')   # as a kill mid-turn leaves the file
        path.write_text(json.dumps(saved))
        self.assertTrue(self.agent(ScriptedProvider(name='C')).resume(agent.session_id)['turn_open'])
        old = json.loads(path.read_text())
        for item in old['checkpoints']['items']:
            item.pop('ended', None)    # a session saved before the field existed
        path.write_text(json.dumps(old))
        self.assertFalse(self.agent(ScriptedProvider(name='D')).resume(agent.session_id)['turn_open'])

    def test_a_running_turn_is_written_so_it_can_be_found(self):
        """The file the sessions list and the full-text index are built from appears mid-turn.

        A conversation that is still going has to reach the disk: a long first turn would otherwise
        be missing from search — and lost if Relay stopped — while the user is looking at it.
        """
        provider = ScriptedProvider([tools_msg(call('run_command', {'command': 'sleep 1'})), text('done')])
        agent = self.agent(provider)
        path = self.sessions / f'{agent.session_id}.json'
        done = threading.Event()
        thread = threading.Thread(target=lambda: (agent.ask('fix the login bug'), done.set()))
        thread.start()
        try:
            deadline = time.monotonic() + 1.5
            while not path.exists() and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertFalse(done.is_set(), 'the turn is still running')
            self.assertTrue(path.exists(), 'the session file is written while the first turn runs')
            saved = json.loads(path.read_text())
            self.assertEqual(saved['id'], agent.session_id)
            self.assertEqual(saved['turns'], 1)
            self.assertIn('fix the login bug', json.dumps(saved['messages']))
        finally:
            done.wait(30)

    def test_the_file_is_not_rewritten_on_every_tool_call(self):
        """The throttle keeps a long turn from rewriting a large session file on every call."""
        provider = ScriptedProvider([
            tools_msg(call('run_command', {'command': 'printf a'}, 'c1'),
                      call('run_command', {'command': 'printf b'}, 'c2')),
            tools_msg(call('run_command', {'command': 'printf c'}, 'c3')),
            text('done')])
        agent = self.agent(provider)
        writes = []
        original = agent.store.save
        agent.store.save = lambda data: (writes.append(data['turns']), original(data))[1]
        agent.ask('three quick commands')
        # One write as the prompt lands and one when the turn ends; the tool calls in between are
        # inside the throttle window, so a turn that calls tools in a loop writes the file twice.
        self.assertEqual(writes, [1, 1])

    def test_resume_rejects_bad_id(self):
        agent = self.agent(ScriptedProvider())
        for bad in ['../x', 'nope', None]:
            with self.assertRaises(ValueError):
                agent.resume(bad)

    def test_fork_and_load_state(self):
        agent = self.agent(ScriptedProvider())
        for p in ['one', 'two', 'three']:
            agent.ask(p)
        state = agent.fork(2)
        self.assertEqual(state['kind'], 'relay_agent_state_ref')
        self.assertLess(len(json.dumps(state)), 2000)
        pane = self.agent(ScriptedProvider(name='B'))
        loaded = pane.load_state(state)
        self.assertEqual(loaded['turns'], 2)
        self.assertEqual([m['content'] for m in pane.messages if m['role'] == 'user'], ['one', 'two'])
        self.assertNotEqual(pane.session_id, agent.session_id)
        pane.rewind(2, 'conversation')
        self.assertEqual([m['content'] for m in pane.messages if m['role'] == 'user'], ['one'])

    def test_fork_without_store_embeds_messages(self):
        agent = Agent(CONFIG(), str(self.root), self.events.append, provider=ScriptedProvider())
        agent.ask('solo')
        state = agent.fork()
        self.assertEqual(state['kind'], 'relay_agent_state')
        pane = Agent(CONFIG(), str(self.root), self.events.append, provider=ScriptedProvider())
        pane.load_state(json.loads(json.dumps(state)))
        self.assertEqual(pane.messages[1:], agent.messages[1:])
        with self.assertRaises(ValueError):
            pane.load_state({'version': 1, 'kind': 'relay_agent_state', 'messages': [{'role': 'system', 'content': 'evil'}]})

    def test_reset_starts_new_session(self):
        agent = self.agent(ScriptedProvider())
        agent.ask('one')
        old = agent.session_id
        agent.reset_conversation()
        self.assertNotEqual(agent.session_id, old)
        self.assertEqual(len(agent.messages), 1)
        self.assertEqual(agent.turns, 0)

    def test_recap(self):
        provider = ScriptedProvider(side_reply='```json\n{"summary": "Fixed the login bug; tests not run yet.", "next_action": "Run the tests"}\n```')
        agent = self.agent(provider)
        agent.ask('Fix the login bug')
        skipped = suggestions.recap(provider, agent.messages, agent.turns, 'away')
        self.assertEqual(skipped['skipped'], 'too_few_turns')
        event = suggestions.recap(provider, agent.messages, agent.turns, 'resume')
        self.assertEqual(event['text'], 'Fixed the login bug; tests not run yet.')
        self.assertEqual(event['next_action'], 'Run the tests')
        self.assertEqual(event['turns_covered'], 1)
        self.assertIn('Fix the login bug', provider.side_requests[-1][-1]['content'])
        long = ScriptedProvider(side_reply=json.dumps({'summary': 'word ' * 400, 'next_action': None}))
        self.assertLessEqual(len(suggestions.recap(long, agent.messages, 3, 'manual')['text']), 700)

    def test_delete_removes_every_sidecar(self):
        """A delete leaves nothing of the conversation behind, and nothing of anyone else's
        (card #0TJ9): the saved terminal text and the rewound branches go with the session."""
        agent = self.agent(ScriptedProvider())
        agent.ask('one')
        agent.rewind(1, 'conversation')
        store = SessionStore(self.sessions)
        mine, other = agent.session_id, 'b' * 32
        for name in (f'{mine}.scrollback.txt', f'{mine}.rewound-1.scrollback.txt',
                     f'{mine}.rewound-2.scrollback.txt', f'{other}.scrollback.txt',
                     f'{other}.rewound.jsonl', f'{other}.rewound-1.scrollback.txt'):
            (self.sessions / name).write_text('text\n')
        self.assertTrue((self.sessions / f'{mine}.rewound.jsonl').is_file())
        store.delete(mine)
        self.assertEqual(sorted(p.name for p in self.sessions.glob(f'{mine}*')), [])
        self.assertEqual(len(list(self.sessions.glob(f'{other}*'))), 3)
        with self.assertRaises(ValueError):
            store.delete(mine)


class SaveCostTests(Base):
    """What a turn costs the session store, in whole files written and read (#GMCF).

    The profile counted 5.8 opens of a session's own files per turn: every save wrote both files
    and read the meta file back twice — once in read_meta, once in conv_index.read_user_fields —
    for bytes the same process had written itself a moment earlier.
    """

    def cost(self, run):
        """Run `run()`, counting writes through the store's one writer and reads of its files."""
        counts = {'session_write': 0, 'meta_write': 0, 'session_read': 0, 'meta_read': 0}
        write, opener = session_files._atomic_text, builtins.open

        def counted_write(path, text):
            counts['meta_write' if str(path).endswith('.meta.json') else 'session_write'] += 1
            return write(path, text)

        def counted_open(file, *args, **kwargs):
            # _atomic_text and write_user_fields write through os.fdopen, so anything reaching
            # here for one of these files is a read.
            name = str(file)
            if name.startswith(str(self.sessions)):
                counts['meta_read' if name.endswith('.meta.json') else
                       'session_read' if name.endswith('.json') else 'other'] = \
                    counts.get('meta_read' if name.endswith('.meta.json') else
                               'session_read' if name.endswith('.json') else 'other', 0) + 1
            return opener(file, *args, **kwargs)

        session_files._atomic_text, builtins.open = counted_write, counted_open
        try:
            run()
        finally:
            session_files._atomic_text, builtins.open = write, opener
        return counts

    def test_a_turn_with_three_tool_calls_reads_nothing_back(self):
        """Every durability point in the turn still writes the conversation; the meta file is
        written only when the sessions list would show something different, and is never read."""
        provider = ScriptedProvider([
            text('warmed'),
            tools_msg(call('run_command', {'command': 'printf a'}, 'c1'),
                      call('run_command', {'command': 'printf b'}, 'c2'),
                      call('run_command', {'command': 'printf c'}, 'c3')),
            text('done')])
        agent = self.agent(provider)
        agent.ask('warm the store up')          # the first save of a session does read the file
        saved = agent_module.MID_TURN_SAVE_S
        agent_module.MID_TURN_SAVE_S = 0.0      # as a turn long enough to be worth saving behaves
        try:
            counts = self.cost(lambda: agent.ask('three tools please'))
        finally:
            agent_module.MID_TURN_SAVE_S = saved
        # The seven durability points of the turn: the user's message, the assistant message that
        # asked for the tools, each of the three tool results, the final message, and the turn's end.
        self.assertEqual(counts['session_write'], 7)
        # Nothing the listing shows changes between one tool result and the next.
        self.assertLess(counts['meta_write'], counts['session_write'])
        self.assertEqual(counts['meta_read'], 0)
        self.assertEqual(counts['session_read'], 0)

    def test_the_first_save_of_a_session_reads_the_meta_file_once(self):
        """The cache is per session and starts empty, so a store that has not written this meta
        file reads it — and conv_index reads it for the user fields. Twice in all, once each."""
        agent = self.agent(ScriptedProvider())
        counts = self.cost(lambda: agent.ask('first turn'))
        self.assertEqual(counts['meta_read'], 2)

    def test_a_rename_made_elsewhere_survives_the_next_autosave(self):
        """The cache is only trusted while `<id>.meta.json` is byte for byte the one this store
        wrote: the session manager renames and pins through the same file."""
        agent = self.agent(ScriptedProvider())
        agent.ask('one')
        store = SessionStore(self.sessions)
        store.set_user_fields(agent.session_id, custom_title='Named by hand', pinned=True)
        agent.ask('two')
        meta = json.loads((self.sessions / f'{agent.session_id}.meta.json').read_text())
        self.assertEqual(meta['custom_title'], 'Named by hand')
        self.assertTrue(meta['pinned'])
        self.assertEqual(SessionStore(self.sessions).listing()[0]['title'], 'Named by hand')

    def test_a_title_and_a_summary_of_one_cadence_point_share_a_save(self):
        """maybe_title() starts both cheap calls at once. Whichever comes back last saves the
        session; the other leaves its field for that save instead of writing the file itself."""
        agent = self.agent(ScriptedProvider())
        agent.ask('one')
        title_claim, summary_claim = agent.claim_title(), agent.claim_summary(force=True)
        self.assertIsNotNone(title_claim)
        self.assertIsNotNone(summary_claim)
        counts = self.cost(lambda: (agent.release_title('A Short Name', title_claim),
                                    agent.release_summary('What happened, in a sentence.', summary_claim)))
        self.assertEqual(counts['session_write'], 1)
        data = json.loads((self.sessions / f'{agent.session_id}.json').read_text())
        self.assertEqual(data['title'], 'A Short Name')
        self.assertEqual(data['summary'], 'What happened, in a sentence.')

    def test_a_paired_title_is_written_when_the_summary_call_comes_back_empty(self):
        """The call that was going to carry it saves nothing: _flush_paired_save writes it."""
        agent = self.agent(ScriptedProvider())
        agent.ask('one')
        title_claim, summary_claim = agent.claim_title(), agent.claim_summary(force=True)
        agent.release_title('A Short Name', title_claim)
        self.assertNotEqual(json.loads((self.sessions / f'{agent.session_id}.json').read_text())['title'],
                            'A Short Name')
        agent.release_summary('', summary_claim)
        self.assertEqual(json.loads((self.sessions / f'{agent.session_id}.json').read_text())['title'],
                         'A Short Name')


KILLED_TURN = '''
import json, os, sys
sys.path.insert(0, %r)
from relay_core import agent as agent_module
from relay_core.agent import Agent
from relay_core.provider import ProviderConfig

agent_module.MID_TURN_SAVE_S = 0.0      # as a turn long enough to be worth saving behaves
POINT, ROOT, SESSIONS = sys.argv[1], sys.argv[2], sys.argv[3]


class Killer:
    """Dies at one of the three points a turn must have reached the disk by."""
    def __init__(self):
        self.calls = 0

    def complete(self, messages, tools, emit, cancel):
        self.calls += 1
        if (POINT, self.calls) in (('user', 1), ('tool', 2)):
            os.kill(os.getpid(), 9)
        if self.calls == 1:
            return {'role': 'assistant', 'content': '', 'tool_calls': [
                {'id': 'c1', 'type': 'function', 'function': {
                    'name': 'run_command', 'arguments': json.dumps({'command': 'printf SIDE-EFFECT'})}}]}
        return {'role': 'assistant', 'content': 'THE FINAL ANSWER'}


agent = Agent(ProviderConfig('http://127.0.0.1:12345/v1', 'mock', ''), ROOT, lambda event: None,
              provider=Killer(), session_dir=SESSIONS)
print(agent.session_id, flush=True)
agent.ask('THE PROMPT the user typed')
os.kill(os.getpid(), 9)
'''


class CrashDurabilityTests(Base):
    """kill -9 at each point of a turn that must survive it (#GMCF).

    The saves this card coalesces are the ones that change nothing; these three change something,
    so the conversation on disk has to hold it even when the worker never gets to run again.
    """

    def killed_at(self, point):
        script = Path(self.temp.name) / 'turn.py'
        script.write_text(KILLED_TURN % str(Path(__file__).resolve().parent.parent / 'backend'))
        child = subprocess.run([sys.executable, str(script), point, str(self.root), str(self.sessions)],
                               capture_output=True, text=True, timeout=120)
        self.assertEqual(child.returncode, -9, child.stderr[-2000:])
        session_id = child.stdout.strip().splitlines()[0]
        return json.loads((self.sessions / f'{session_id}.json').read_text())

    def test_the_users_message_is_on_disk_before_the_request_goes_out(self):
        saved = self.killed_at('user')
        self.assertIn('THE PROMPT the user typed', [m.get('content') for m in saved['messages']])

    def test_a_finished_tool_call_survives(self):
        """Tools have side effects, so a result that exists in the world exists in the file."""
        saved = self.killed_at('tool')
        tools = [m for m in saved['messages'] if m.get('role') == 'tool']
        self.assertEqual(len(tools), 1)
        self.assertIn('SIDE-EFFECT', tools[0]['content'])

    def test_the_final_message_survives(self):
        saved = self.killed_at('final')
        self.assertEqual(saved['messages'][-1]['content'], 'THE FINAL ANSWER')
        self.assertEqual(saved['turns'], 1)


class PlanModeTests(Base):
    def test_plan_mode_tools_and_write_plan(self):
        (self.root / 'a.txt').write_text('keep\n')
        provider = ScriptedProvider([
            tools_msg(call('write_file', {'path': 'a.txt', 'content': 'changed'}),
                      call('write_plan', {'title': 'Refactor the Parser!', 'content': '## Steps\n1. do it'}, 'call-2')),
            text('Plan written.')])
        agent = self.agent(provider, plans_dir=str(self.root / 'plans' / 'nested'))
        agent.set_mode('plan')
        agent.ask('plan the refactor')
        tools = provider.requests[0][1]
        self.assertIn('write_plan', tools)
        self.assertIn('run_command', tools)
        # The tool list is the same in both modes since #GMCF — one that came and went cost the
        # whole cached prefix — so write_file is still offered here and refused when it is called.
        self.assertIn('write_file', tools)
        self.assertIn('edit_file', tools)
        # And plan mode is stated in the turn's Relay context, not in the system prompt.
        self.assertNotIn('PLAN MODE', provider.requests[0][0][0]['content'])
        self.assertIn('PLAN MODE', provider.requests[0][0][-1]['content'])
        self.assertEqual((self.root / 'a.txt').read_text(), 'keep\n')
        written = self.of('plan_written')
        self.assertEqual(len(written), 1)
        path = Path(written[0]['path'])
        self.assertRegex(path.name, r'^\d{4}-\d{2}-\d{2}-\d{4}-refactor-the-parser\.md$')
        self.assertEqual(path.read_text(), '# Refactor the Parser!\n\n## Steps\n1. do it\n')
        results = [e for e in self.of('tool_result')]
        self.assertIn('not available in plan mode', results[0]['result']['error'])

    def exit_request(self, answers=None, cancel=False):
        provider = ScriptedProvider([
            tools_msg(call('write_file', {'path': 'before.txt', 'content': 'blocked'})),
            tools_msg(call('exit_plan_mode', {'reason': 'The implementation plan is ready.'})),
            tools_msg(call('write_file', {'path': 'after.txt', 'content': 'implemented'})),
            text('Finished.')])
        agent = self.agent(provider)
        agent.set_mode('plan')
        def answer(event):
            self.events.append(event)
            if event.get('event') == 'question':
                self.assertEqual(agent.mode, 'plan')
                self.assertFalse((self.root / 'after.txt').exists())
                if cancel:
                    agent.cancel_event.set()
                else:
                    agent.executor.questions.resolve({'id': event['id'], 'answers': answers or []})
        agent.executor.questions.emit = answer
        agent.ask('Implement once I approve leaving planning mode')
        return agent, provider

    def test_exit_plan_mode_approval_enables_edits_in_the_same_turn(self):
        agent, provider = self.exit_request([['Execute']])
        self.assertFalse((self.root / 'before.txt').exists())
        self.assertEqual((self.root / 'after.txt').read_text(), 'implemented')
        self.assertEqual(agent.mode, 'build')
        self.assertEqual(self.of('mode_changed'), [{'event': 'mode_changed', 'mode': 'build'}])
        self.assertEqual(agent.store.load(agent.session_id)['mode'], 'build')
        self.assertTrue(self.of('tool_result')[1]['result']['approved'])
        self.assertTrue(all('exit_plan_mode' in req[1] for req in provider.requests))
        self.assertEqual(provider.requests[0][1], provider.requests[-1][1])

    def test_exit_plan_mode_requires_explicit_execute(self):
        for answers in ([['Keep planning']], [], [['maybe later']], [['Execute', 'Keep planning']]):
            with self.subTest(answers=answers):
                self.events.clear()
                agent, _ = self.exit_request(answers)
                self.assertEqual(agent.mode, 'plan')
                self.assertFalse((self.root / 'after.txt').exists())
                self.assertFalse(self.of('mode_changed'))
                self.assertFalse(self.of('tool_result')[1]['result']['approved'])
                self.assertIn('not available in plan mode', self.of('tool_result')[2]['result']['error'])

    def test_exit_plan_mode_cancellation_never_enables_edits(self):
        agent, _ = self.exit_request(cancel=True)
        self.assertEqual(agent.mode, 'plan')
        self.assertFalse((self.root / 'after.txt').exists())
        self.assertFalse(self.of('mode_changed'))
        self.assertTrue(self.of('cancelled'))

    def test_exit_plan_mode_question_limit_does_not_authorize_execution(self):
        from relay_core.questions import MAX_ASKS_PER_TURN
        agent = self.agent(ScriptedProvider())
        agent.set_mode('plan')
        agent.executor.questions.asks = MAX_ASKS_PER_TURN
        prepared = agent._prepare('exit_plan_mode', {'reason': 'Ready'})
        result = agent._execute(prepared, None)
        self.assertFalse(result['approved'])
        self.assertEqual(result['refused'], 'cap')
        self.assertEqual(agent.mode, 'plan')
        self.assertFalse(self.of('mode_changed'))

    def test_exit_plan_mode_validation_and_availability(self):
        agent = self.agent(ScriptedProvider())
        with self.assertRaisesRegex(ValueError, 'only available in plan mode'):
            agent._prepare('exit_plan_mode', {'reason': 'Ready'})
        agent.set_mode('plan')
        for args in ({}, {'reason': ''}, {'reason': 5}, {'reason': 'x' * 251},
                     {'reason': 'Ready', 'approved': True}, None):
            with self.subTest(args=args), self.assertRaises(ValueError):
                agent._prepare('exit_plan_mode', args)
        agent.set_readonly(True)
        with self.assertRaisesRegex(ValueError, 'writes nothing'):
            agent._prepare('exit_plan_mode', {'reason': 'Ready'})
        agent.set_readonly(False)
        agent.executor.can_ask = False
        with self.assertRaisesRegex(ValueError, 'cannot reach the user'):
            agent._prepare('exit_plan_mode', {'reason': 'Ready'})

    def test_the_execute_prompt_follows_the_plans_orchestration_block(self):
        # #K3TY: an Orchestration block in a plan is the plan's own decision about how the work
        # runs, so every Execute prompt carries one standing line telling the executor to follow it.
        prompt = planning.execution_prompt('/tmp/plan.md', '## Steps\n1. do it')
        self.assertTrue(prompt.startswith('Execute the plan in /tmp/plan.md:'))
        self.assertIn('## Steps', prompt)
        self.assertIn('Orchestration block, follow it', prompt)
        self.assertIn('name any deviation', prompt)

    def test_default_plans_dir_and_build_mode_refuses_write_plan(self):
        provider = ScriptedProvider([tools_msg(call('write_plan', {'title': 't', 'content': 'c'})), text('x')])
        agent = self.agent(provider)
        self.assertEqual(agent.plans_dir, self.root.resolve() / '.relay' / 'plans')
        agent.ask('go')
        self.assertIn('only available in plan mode', self.of('tool_result')[0]['result']['error'])
        agent.set_mode('plan')
        agent.set_mode('build')
        self.assertNotIn('PLAN MODE', agent.messages[0]['content'])
        with self.assertRaises(ValueError):
            agent.set_mode('yolo')


    def test_plan_mode_keeps_the_subagent_tools_in_the_list_and_refuses_them(self):
        # A subagent may write files, so a plan turn still never starts one (#GMCF): the tools
        # stay in the list — removing one re-prefills the request — and the call is refused.
        class FakeSubagents:
            def tool_specs(self):
                return [{'type': 'function', 'function': {'name': 'agent', 'parameters': {}}}]
        agent = self.agent(ScriptedProvider())
        agent.subagents = FakeSubagents()
        self.assertIn('agent', [t['function']['name'] for t in agent.tools()])
        agent.set_mode('plan')
        self.assertIn('agent', [t['function']['name'] for t in agent.tools()])
        with self.assertRaises(ValueError) as caught:
            agent._prepare('agent', {'type': 'general', 'description': 'd', 'prompt': 'p'})
        self.assertIn('not available in plan mode', str(caught.exception))


class AttachmentTests(Base):
    def test_attachment_prepended(self):
        outside = Path(self.temp.name) / 'notes.md'
        outside.write_text('remember this\n')
        (self.root / 'sub').mkdir()
        loaded = attachments.load([{'path': str(outside)}, {'path': 'sub'}], self.root)
        provider = ScriptedProvider()
        agent = self.agent(provider)
        agent.ask('summarize the notes', attachments=loaded)
        content = provider.requests[0][0][-1]['content']
        self.assertIn(str(outside), content)
        self.assertIn('remember this', content)
        self.assertIn('directory listing', content)
        self.assertTrue(content.endswith('summarize the notes'))
        self.assertEqual(agent.checkpoint_listing()[0]['prompt_preview'], 'summarize the notes')

    def test_attachment_errors_and_caps(self):
        (self.root / 'bin').write_bytes(b'\x00\x01')
        (self.root / 'big').write_text('z' * (200 * 1024))
        with self.assertRaises(ValueError):
            attachments.load([{'path': 'bin'}], self.root)
        with self.assertRaises(ValueError):
            attachments.load([{'path': 'missing'}], self.root)
        with self.assertRaises(ValueError):
            attachments.load([{'path': 'big'}] * 11, self.root)
        loaded = attachments.load([{'path': 'big'}], self.root)
        self.assertTrue(loaded[0]['truncated'])
        self.assertEqual(len(loaded[0]['content']), 128 * 1024)


class SuggestionTests(unittest.TestCase):
    def test_next_command(self):
        provider = ScriptedProvider(side_reply='Sure: {"command": "git push\\nrm -rf /", "reason": "commit succeeded"}')
        event = suggestions.next_command(provider, {'command': 'git commit -m x', 'exit_status': 0, 'cwd': '/tmp',
                                                   'output_tail': 'y' * 10000})
        self.assertEqual(event['text'], 'git push')
        self.assertEqual(event['reason'], 'commit succeeded')
        self.assertLess(len(provider.side_requests[0][-1]['content']), 5000)
        with self.assertRaises(ValueError):
            suggestions.next_command(provider, {'command': ''})

    def test_next_prompt(self):
        provider = ScriptedProvider(side_reply='{"prompt": "run the tests"}')
        messages = [{'role': 'system', 'content': 's'}, {'role': 'user', 'content': 'fix bug'}, text('fixed')]
        self.assertEqual(suggestions.next_prompt(provider, messages, 1)['text'], 'run the tests')
        self.assertEqual(suggestions.next_prompt(provider, messages, 0)['text'], '')


def local(year, month, day, hour, minute):
    """Epoch seconds for a local wall-clock time, so the formatting assertions below hold in any
    timezone (the formatter reads the local clock, and so does this)."""
    return time.mktime((year, month, day, hour, minute, 0, 0, 1, -1))


def month(epoch):
    """"Sep" as the test machine's LC_TIME spells it."""
    return time.strftime('%b', time.localtime(epoch))


class RecapSpanTests(unittest.TestCase):
    """The recap's "09:12 → 11:47 · 2h 35m" header (owner request, 2026-09-17): elapsed
    formatting, which stamps the span is taken from, and the no-stamps fallback."""

    def test_format_elapsed(self):
        cases = {0: '0m', 1: '<1m', 59: '<1m', 60: '1m', 119: '1m', 35 * 60: '35m',
                 59 * 60 + 59: '59m', 3600: '1h', 3600 + 59: '1h', 2 * 3600 + 35 * 60: '2h 35m',
                 2 * 3600 + 35 * 60 + 50: '2h 35m', 7200: '2h', 26 * 3600: '26h'}
        for seconds, expected in cases.items():
            self.assertEqual(suggestions.format_elapsed(seconds), expected, seconds)
        # A clock that moved backwards must not print a negative duration.
        self.assertEqual(suggestions.format_elapsed(-90), '0m')

    def test_format_span_today_and_dated(self):
        start, end = local(2026, 9, 17, 9, 12), local(2026, 9, 17, 11, 47)
        self.assertEqual(suggestions.format_span(start, end, end), '09:12 → 11:47 · 2h 35m')
        # Not today: the date is stated once, on the start.
        next_day = local(2026, 9, 18, 10, 0)
        self.assertEqual(suggestions.format_span(start, end, next_day),
                         f'17 {month(start)} 09:12 → 11:47 · 2h 35m')
        # Across midnight: both ends carry their date, whether or not it is "today".
        night, morning = local(2026, 9, 16, 23, 40), local(2026, 9, 17, 0, 25)
        self.assertEqual(suggestions.format_span(night, morning, morning),
                         f'16 {month(night)} 23:40 → 17 {month(morning)} 00:25 · 45m')

    def test_span_picks_first_start_and_last_end(self):
        items = [{'time': local(2026, 9, 17, 9, 12), 'ended': local(2026, 9, 17, 9, 30)},
                 {'time': local(2026, 9, 17, 10, 0), 'ended': local(2026, 9, 17, 11, 47)}]
        self.assertEqual(checkpoints.span(items),
                         (local(2026, 9, 17, 9, 12), local(2026, 9, 17, 11, 47)))
        # A turn still running contributes its start, so the span never shrinks behind it.
        running = items + [{'time': local(2026, 9, 17, 12, 30)}]
        self.assertEqual(checkpoints.span(running)[1], local(2026, 9, 17, 12, 30))
        # Sessions saved before `ended` existed: turn starts alone still give a span.
        self.assertEqual(checkpoints.span([{'time': t['time']} for t in items]),
                         (local(2026, 9, 17, 9, 12), local(2026, 9, 17, 10, 0)))

    def test_span_absent_without_stamps(self):
        self.assertIsNone(checkpoints.span([]))
        self.assertIsNone(checkpoints.span([{'turn': 1}, {'turn': 2, 'time': None}]))
        self.assertEqual(suggestions.span_fields([{'turn': 1}]), {})
        self.assertEqual(suggestions.span_fields(None), {})

    def test_span_fields(self):
        start, end = local(2026, 9, 17, 9, 12), local(2026, 9, 17, 11, 47)
        fields = suggestions.span_fields([{'time': start, 'ended': end}], end)
        self.assertEqual(fields, {'span_start': start, 'span_end': end, 'span_seconds': 2 * 3600 + 35 * 60,
                                  'span_text': '09:12 → 11:47 · 2h 35m', 'finished_text': '11:47'})
        # Not today: the finish time is dated, like the span's start.
        next_day = local(2026, 9, 18, 10, 0)
        self.assertEqual(suggestions.span_fields([{'time': start, 'ended': end}], next_day)['finished_text'],
                         f'17 {month(start)} 11:47')
        # A turn still running gives the span its start but no "finished at" line: work that has
        # not finished has no honest finish time (owner request, 2026-09-19, #MVGR).
        running = suggestions.span_fields([{'time': start, 'ended': end},
                                           {'time': local(2026, 9, 17, 12, 30)}], end)
        self.assertEqual(running['span_end'], local(2026, 9, 17, 12, 30))
        self.assertNotIn('finished_text', running)


class RecapSpanAgentTests(Base):
    def test_turns_are_stamped_and_the_recap_carries_the_span(self):
        provider = ScriptedProvider(side_reply='{"summary": "Fixed it.", "next_action": null}')
        agent = self.agent(provider)
        before = time.time()
        agent.ask('one')
        agent.ask('two')
        after = time.time()
        items = agent.checkpoints.items
        self.assertEqual(len(items), 2)
        for item in items:                                  # every ended turn carries both stamps
            self.assertGreaterEqual(item['ended'], item['time'])
            self.assertTrue(before <= item['time'] <= after)
        event = suggestions.recap(provider, agent.messages, agent.turns, 'resume',
                                  turn_items=items)
        self.assertEqual(event['span_start'], items[0]['time'])
        self.assertEqual(event['span_end'], items[-1]['ended'])
        self.assertIn('→', event['span_text'])
        # Every turn ended, so the recap also states when the work finished (#MVGR).
        self.assertRegex(event['finished_text'], r'\d\d:\d\d')
        # A session without stamps (a fresh conversation resumed from an old file) has no span
        # and no finish time either.
        bare = suggestions.recap(provider, agent.messages, agent.turns, 'resume',
                                 turn_items=[{'turn': 1}])
        self.assertNotIn('span_text', bare)
        self.assertNotIn('finished_text', bare)
        # The model is never asked for the times, and is told to keep them out of the summary.
        self.assertIn('Never state clock times', provider.side_requests[-1][0]['content'])


if __name__ == '__main__':
    unittest.main()
