"""Agent sessions: model/effort switching, context and compaction, checkpoints, rewind, fork,
sessions and recaps, plan mode, attachments. Fake providers only; no network."""
import json
import os
import re
import tempfile
import threading
import unittest
from pathlib import Path

from relay_core import attachments, suggestions
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
        compacted = self.of('compacted')[-1]
        self.assertLess(compacted['after_tokens'], compacted['before_tokens'])
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
        self.assertNotIn('write_file', tools)
        self.assertIn('run_command', tools)
        self.assertIn('PLAN MODE', provider.requests[0][0][0]['content'])
        self.assertEqual((self.root / 'a.txt').read_text(), 'keep\n')
        written = self.of('plan_written')
        self.assertEqual(len(written), 1)
        path = Path(written[0]['path'])
        self.assertRegex(path.name, r'^\d{4}-\d{2}-\d{2}-\d{4}-refactor-the-parser\.md$')
        self.assertEqual(path.read_text(), '# Refactor the Parser!\n\n## Steps\n1. do it\n')
        results = [e for e in self.of('tool_result')]
        self.assertIn('not available in plan mode', results[0]['result']['error'])

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


    def test_plan_mode_hides_subagent_tools(self):
        class FakeSubagents:
            def tool_specs(self):
                return [{'type': 'function', 'function': {'name': 'agent', 'parameters': {}}}]
        agent = self.agent(ScriptedProvider())
        agent.subagents = FakeSubagents()
        self.assertIn('agent', [t['function']['name'] for t in agent.tools()])
        agent.set_mode('plan')
        self.assertNotIn('agent', [t['function']['name'] for t in agent.tools()])


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


if __name__ == '__main__':
    unittest.main()
