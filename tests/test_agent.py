import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

from relay_core.agent import Agent
from relay_core.provider import ProviderConfig, ProviderStalled, ProviderTruncated

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ProviderConfig('http://127.0.0.1:12345/v1', 'mock', '')

def tool(name, arguments):
    return {'role':'assistant','content':'','reasoning_content':'provider reasoning to preserve',
            'tool_calls':[{'id':'call-1','type':'function','function':{'name':name,'arguments':json.dumps(arguments)}}]}

class FakeProvider:
    def __init__(self, response): self.response=response; self.calls=0; self.messages=[]
    def complete(self, messages, tools, emit, cancel):
        self.messages = json.loads(json.dumps(messages))
        self.calls += 1
        if self.calls == 1: return self.response
        emit({'event':'delta','text':'Finished.'})
        return {'role':'assistant','content':'Finished.'}
    def cancel(self): pass

class AgentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.root=Path(self.temp.name)
    def tearDown(self): self.temp.cleanup()

    def test_command_runs_without_approval(self):
        events=[]
        fake=FakeProvider(tool('run_command', {'command': 'printf HELLO; touch sentinel'}))
        agent=Agent(CONFIG,self.temp.name,events.append,provider=fake)
        agent.ask('create a sentinel')
        self.assertTrue((self.root/'sentinel').exists())
        self.assertFalse(any(e['event']=='approval' for e in events))
        started=[e for e in events if e['event']=='tool_started']
        self.assertEqual(len(started),1)
        self.assertIn('touch sentinel', started[0]['preview'])
        self.assertEqual(events[-1]['event'],'done')
        self.assertEqual(fake.messages[-2]['reasoning_content'],'provider reasoning to preserve')
        self.assertIn('HELLO', fake.messages[-1]['content'])

    def test_write_runs_without_approval_and_shows_diff(self):
        events=[]
        fake=FakeProvider(tool('write_file',{'path':'note.txt','content':'hello\n'}))
        agent=Agent(CONFIG,self.temp.name,events.append,provider=fake); agent.ask('write something')
        self.assertEqual((self.root/'note.txt').read_text(),'hello\n')
        preview=[e for e in events if e['event']=='tool_started'][0]['preview']
        self.assertIn('+hello', preview)

    def test_unknown_tool_is_not_executed(self):
        fake=FakeProvider(tool('unknown',{})); events=[]
        agent=Agent(CONFIG,self.temp.name,events.append,provider=fake); agent.ask('hi')
        self.assertFalse(any(e['event']=='tool_started' for e in events))
        self.assertIn('error', json.loads(fake.messages[-1]['content']))

    def test_file_tools_still_confined_to_workspace(self):
        fake=FakeProvider(tool('read_file',{'path':'../outside.txt'})); events=[]
        agent=Agent(CONFIG,self.temp.name,events.append,provider=fake); agent.ask('read it')
        self.assertFalse(any(e['event']=='tool_started' for e in events))
        self.assertIn('error', json.loads(fake.messages[-1]['content']))

    def test_cancel_while_command_runs(self):
        fake=FakeProvider(tool('run_command',{'command':'sleep 20; touch sentinel','timeout_seconds':60}))
        events=[]; running=threading.Event()
        def emit(event):
            events.append(event)
            if event['event']=='tool_started': running.set()
        agent=Agent(CONFIG,self.temp.name,emit,provider=fake)
        thread=threading.Thread(target=agent.ask,args=('do a thing',)); thread.start()
        self.assertTrue(running.wait(2)); time.sleep(0.2); agent.stop(); thread.join(5)
        self.assertFalse(thread.is_alive()); self.assertFalse((self.root/'sentinel').exists())
        self.assertEqual(events[-1]['event'],'cancelled')
        # G2: the prompt stays; the interrupted tool-call group is completed with a "not completed" result.
        self.assertEqual(agent.messages[1]['content'], 'do a thing')
        calls=[c['id'] for m in agent.messages if m.get('tool_calls') for c in m['tool_calls']]
        results=[m['tool_call_id'] for m in agent.messages if m['role']=='tool']
        self.assertEqual(calls, results)
        self.assertIn('not finished', agent.messages[-1]['content'])

class SystemPromptTests(unittest.TestCase):
    def test_replies_are_asked_for_in_markdown(self):
        # The GUI renders agent replies as Markdown in the terminal (src/MarkdownAnsi.cpp).
        from relay_core import agent as agent_module
        self.assertIn("Format replies as Markdown", agent_module.SYSTEM)
        self.assertIn("fenced code blocks", agent_module.SYSTEM)


class StallRetryTests(unittest.TestCase):
    """Issue SQAM: a stalled model call is retried once, and only when nothing of the answer arrived."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.root = Path(self.temp.name)
    def tearDown(self): self.temp.cleanup()

    class StallingProvider:
        """Stalls on the first `stalls` calls, then answers. Records what it was asked to send."""
        def __init__(self, stalls=1, produced=False):
            self.stalls, self.produced, self.calls, self.sent = stalls, produced, 0, []
        def complete(self, messages, tools, emit, cancel):
            self.calls += 1
            self.sent.append(json.loads(json.dumps(messages)))
            if self.calls <= self.stalls:
                emit({'event': 'thinking_delta', 'text': 'reasoning that goes nowhere'})
                raise ProviderStalled(60.0, self.produced)
            emit({'event': 'delta', 'text': 'Finished.'})
            return {'role': 'assistant', 'content': 'Finished.'}
        def cancel(self): pass

    def agent(self, provider, events):
        return Agent(CONFIG, self.temp.name, events.append, provider=provider)

    def test_retried_once_and_the_second_try_sends_the_same_conversation(self):
        provider = self.StallingProvider(stalls=1); events = []
        agent = self.agent(provider, events); agent.ask('do the thing')
        self.assertEqual(provider.calls, 2)
        self.assertEqual(provider.sent[0], provider.sent[1])   # no side effect, nothing added or lost
        retries = [e for e in events if e['event'] == 'provider_retry']
        self.assertEqual(len(retries), 1)
        self.assertEqual(retries[0]['attempt'], 1)
        self.assertIn('sent nothing for 60 s', retries[0]['text'])
        self.assertEqual(events[-1]['event'], 'done')
        # The stalled thinking block is closed before the retry, so the overlay does not stay open.
        self.assertTrue(any(e['event'] == 'thinking_done' for e in events))
        self.assertEqual([e for e in events if e['event'] == 'turn_summary'][-1]['outcome'], 'done')

    def test_a_second_stall_fails_the_turn_and_keeps_the_request_open(self):
        provider = self.StallingProvider(stalls=2); events = []
        agent = self.agent(provider, events); agent.ask('do the thing')
        self.assertEqual(provider.calls, 2)                    # one try, one retry, then it stops
        self.assertEqual(events[-1]['event'], 'error')
        self.assertIn('sent nothing for 60 s', events[-1]['text'])
        self.assertEqual(agent.requests.open_count(), 1)       # the request stays open in the ledger
        self.assertEqual(agent.messages[1]['content'], 'do the thing')
        self.assertIn('not finished', agent.messages[-1]['content'])

    def test_a_started_answer_is_not_retried(self):
        provider = self.StallingProvider(stalls=1, produced=True); events = []
        agent = self.agent(provider, events); agent.ask('do the thing')
        self.assertEqual(provider.calls, 1)
        self.assertFalse(any(e['event'] == 'provider_retry' for e in events))
        self.assertEqual(events[-1]['event'], 'error')

    def test_stall_timeout_is_an_agent_option(self):
        provider = self.StallingProvider(stalls=0)
        agent = self.agent(provider, [])
        self.assertEqual(agent.options()['stall_timeout_s'], 60.0)
        self.assertEqual(agent.set_options({'stall_timeout_s': 120})['stall_timeout_s'], 120.0)
        with self.assertRaises(ValueError):
            agent.set_options({'stall_timeout_s': 0})


class TruncationRetryTests(unittest.TestCase):
    """A step that spends its whole output budget without producing anything is taken again once.

    Owner report, 2026-09-18 (session 270a38a3): GLM-5.3 at effort `high` thought for 108 s on one
    step, hit the 32768-token limit with no text and no tool call, and the four-minute turn behind
    it - nine tool results - was thrown away.
    """

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.root = Path(self.temp.name)
    def tearDown(self): self.temp.cleanup()

    class CutOffProvider:
        """Truncates the first `cuts` calls, then answers. Records what it was asked to send."""
        def __init__(self, cuts=1, produced=False, partial=None, reason='length'):
            self.cuts, self.produced, self.partial, self.reason = cuts, produced, partial, reason
            self.calls, self.sent = 0, []
        def complete(self, messages, tools, emit, cancel):
            self.calls += 1
            self.sent.append(json.loads(json.dumps(messages)))
            if self.calls <= self.cuts:
                emit({'event': 'thinking_delta', 'text': 'reasoning that fills the budget'})
                raise ProviderTruncated(self.reason, 32768, self.produced, self.partial)
            emit({'event': 'delta', 'text': 'Finished.'})
            return {'role': 'assistant', 'content': 'Finished.'}
        def cancel(self): pass

    def agent(self, provider, events):
        return Agent(CONFIG, self.temp.name, events.append, provider=provider)

    def test_retried_once_with_a_note_that_says_why(self):
        provider = self.CutOffProvider(cuts=1); events = []
        agent = self.agent(provider, events); agent.ask('do the thing')
        self.assertEqual(provider.calls, 2)
        note = provider.sent[1][-1]        # the retry carries one more message than the first try
        self.assertEqual(len(provider.sent[1]), len(provider.sent[0]) + 1)
        self.assertEqual(note['relay_kind'], 'note')
        self.assertIn('32768-token output limit', note['content'])
        self.assertIn('Reasoning is spent from that same budget', note['content'])
        retries = [e for e in events if e['event'] == 'provider_retry']
        self.assertEqual([r['reason'] for r in retries], ['truncated'])
        self.assertEqual(retries[0]['attempt'], 1)
        self.assertEqual(events[-1]['event'], 'done')
        # The thinking block of the cut-off step is closed, so the overlay does not stay open.
        self.assertTrue(any(e['event'] == 'thinking_done' for e in events))

    def test_a_second_cut_off_fails_the_turn_and_keeps_the_request_open(self):
        provider = self.CutOffProvider(cuts=2); events = []
        agent = self.agent(provider, events); agent.ask('do the thing')
        self.assertEqual(provider.calls, 2)
        self.assertEqual(events[-1]['event'], 'error')
        self.assertIn('whole 32768-token output budget', events[-1]['text'])
        self.assertEqual(agent.requests.open_count(), 1)

    def test_an_answer_that_started_is_not_retried_but_is_kept(self):
        partial = {'role': 'assistant', 'content': 'Half an ans'}
        provider = self.CutOffProvider(cuts=1, produced=True, partial=partial); events = []
        agent = self.agent(provider, events); agent.ask('do the thing')
        self.assertEqual(provider.calls, 1)
        self.assertFalse(any(e['event'] == 'provider_retry' for e in events))
        self.assertEqual(events[-1]['event'], 'error')
        # What the user watched arrive is still in the conversation to carry on from.
        self.assertIn('Half an ans', [m.get('content') for m in agent.messages])

    def test_a_filtered_response_is_not_retried(self):
        provider = self.CutOffProvider(cuts=1, reason='content_filter'); events = []
        agent = self.agent(provider, events); agent.ask('do the thing')
        self.assertEqual(provider.calls, 1)
        self.assertFalse(any(e['event'] == 'provider_retry' for e in events))
        self.assertIn('filtered this response', events[-1]['text'])


class WorkerTests(unittest.TestCase):
    def run_worker(self, messages):
        payload=''.join(json.dumps(m)+'\n' for m in messages)
        proc=subprocess.run([sys.executable,'-S',str(ROOT/'backend/worker.py')],input=payload,
                            text=True,capture_output=True,timeout=5,cwd=ROOT)
        self.assertEqual(proc.returncode,0,proc.stderr)
        return [json.loads(line) for line in proc.stdout.splitlines()]

    def test_route_without_any_provider(self):
        results=self.run_worker([{'type':'route','id':'1','text':'git status'},
                                {'type':'route','id':'2','text':'why did this fail?'}, {'type':'shutdown'}])
        self.assertEqual(results[0]['event'],'ready')
        self.assertEqual(results[1]['route'],'shell')
        self.assertEqual(results[2]['route'],'agent')

    def test_configuration_does_not_contact_provider_or_echo_key(self):
        results=self.run_worker([{'type':'configure','base_url':'http://127.0.0.1:1/v1','model':'test',
                                 'api_key':'SECRET_NEVER_ECHO','workspace':str(ROOT)}, {'type':'shutdown'}])
        self.assertEqual(results[1]['event'],'configured')
        self.assertNotIn('SECRET_NEVER_ECHO',json.dumps(results))

    def test_malformed_request_does_not_crash(self):
        results=self.run_worker([[], {'type':'route','id':'ok','text':'echo hello'},{'type':'shutdown'}])
        self.assertEqual(results[1]['event'],'error')
        self.assertEqual(results[2]['route'],'shell')

    def test_agent_requires_config(self):
        results=self.run_worker([{'type':'ask','text':'hello'},{'type':'shutdown'}])
        self.assertIn('Configure',results[1]['text'])


class ContextTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.temp.cleanup()

    def test_context_note_is_labelled_and_prepended(self):
        fake = FakeProvider({'role': 'assistant', 'content': 'I cannot type into vim.'})
        agent = Agent(CONFIG, self.temp.name, lambda e: None, provider=fake)
        agent.ask('type something interesting', context={'foreground_program': 'vim scratch.txt', 'terminal_cwd': '/tmp/x'})
        content = fake.messages[-1]['content']
        self.assertTrue(content.startswith('[Relay context: added by Relay, not typed by the user]'))
        self.assertIn('`vim scratch.txt`', content)
        self.assertIn('/tmp/x', content)
        self.assertIn('cannot see', content)
        self.assertTrue(content.endswith('type something interesting'))

    def test_no_context_leaves_prompt_unchanged(self):
        fake = FakeProvider({'role': 'assistant', 'content': 'ok'})
        agent = Agent(CONFIG, self.temp.name, lambda e: None, provider=fake)
        agent.ask('hello', context=None)
        self.assertEqual(fake.messages[-1]['content'], 'hello')

    def test_terminal_directory_alone_is_still_context(self):
        # `cd` in the terminal moves the agent: the note is added and run_command follows.
        fake = FakeProvider({'role': 'assistant', 'content': 'ok'})
        agent = Agent(CONFIG, self.temp.name, lambda e: None, provider=fake)
        import os
        os.mkdir(os.path.join(self.temp.name, 'sub'))
        agent.ask('hello', context={'foreground_program': '', 'terminal_cwd': os.path.join(self.temp.name, 'sub')})
        content = fake.messages[-1]['content']
        self.assertIn('`' + os.path.join(self.temp.name, 'sub') + '`', content)
        self.assertTrue(content.endswith('hello'))
        self.assertEqual(agent.executor.default_cwd, 'sub')
        # A directory outside the workspace is ignored rather than escaping it.
        agent.ask('hello', context={'terminal_cwd': '/tmp'})
        self.assertEqual(agent.executor.default_cwd, '.')

    def test_invalid_context_rejected(self):
        from relay_core.agent import validate_context
        with self.assertRaises(ValueError): validate_context({'foreground_program': 'vim', 'extra': 1})
        with self.assertRaises(ValueError): validate_context({'foreground_program': 5})
        with self.assertRaises(ValueError): validate_context({'foreground_program': 'x' * 1001})

    def test_control_characters_stripped_from_program(self):
        from relay_core.agent import format_context
        note = format_context({'foreground_program': 'vim \x1b[31mevil\x07'})
        self.assertNotIn('\x1b', note)
        self.assertNotIn('\x07', note)

    def test_worker_passes_context_to_queue(self):
        payload = ''.join(json.dumps(m) + '\n' for m in [
            {'type': 'configure', 'base_url': 'http://127.0.0.1:1/v1', 'model': 'test', 'api_key': '', 'workspace': str(ROOT)},
            {'type': 'ask', 'text': 'hi', 'context': {'foreground_program': 'vim', 'bogus': 1}},
            {'type': 'shutdown'}])
        proc = subprocess.run([sys.executable, '-S', str(ROOT / 'backend/worker.py')], input=payload,
                              text=True, capture_output=True, timeout=10, cwd=ROOT)
        events = [json.loads(line) for line in proc.stdout.splitlines()]
        self.assertTrue(any(e['event'] == 'error' and 'Context' in e.get('text', '') for e in events))


class ToolLabelEventTests(unittest.TestCase):
    """Card #TK9C, protocol 23: the concise line rides on the tool events, and nothing that was
    already on them moved or changed."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.root = Path(self.temp.name)
    def tearDown(self): self.temp.cleanup()

    def run_tool(self, name, arguments):
        events = []
        fake = FakeProvider(tool(name, arguments))
        agent = Agent(CONFIG, self.temp.name, events.append, provider=fake)
        agent.ask('do it')
        return agent, events

    def of(self, events, kind):
        return [e for e in events if e['event'] == kind]

    def test_run_command_events_carry_the_label(self):
        agent, events = self.run_tool('run_command', {'command': 'printf "one\\ntwo\\n"'})
        started = self.of(events, 'tool_started')[0]
        self.assertEqual(started['label']['kind'], 'run')
        self.assertEqual(started['label']['running'], 'running printf "one\\ntwo\\n"')
        self.assertEqual(started['label']['title'], 'ran printf "one\\ntwo\\n"')
        # Every field the older surfaces read is exactly where it was.
        self.assertEqual(started['tool'], 'run_command')
        self.assertIn('RUN COMMAND', started['preview'])
        self.assertIn('call_id', started)
        self.assertIn('turn_id', started)
        result = self.of(events, 'tool_result')[0]
        self.assertEqual(result['label']['title'], 'ran printf "one\\ntwo\\n"')
        self.assertEqual(result['label']['stats'][0], '2 lines')
        self.assertIn('exit 0', result['label']['stats'])
        self.assertTrue(result['label']['ok'])
        self.assertIsInstance(result['ms'], int)
        self.assertEqual(result['tool'], 'run_command')
        self.assertEqual(result['result']['exit_code'], 0)
        self.assertNotIn('diff', result)

    def test_a_write_carries_its_diff_and_the_summary_carries_the_label(self):
        agent, events = self.run_tool('write_file', {'path': 'note.txt', 'content': 'hello\n'})
        result = self.of(events, 'tool_result')[0]
        self.assertEqual(result['label']['title'], 'wrote note.txt')
        self.assertEqual(result['label']['stats'], ['new', '1 line'])
        self.assertEqual(result['label']['open'], {'type': 'file', 'path': 'note.txt'})
        self.assertTrue(result['label']['inline_diff'])
        self.assertIn('+hello', result['diff'])
        self.assertNotIn('Old bytes', result['diff'])
        summary = self.of(events, 'turn_summary')[0]
        self.assertEqual(summary['tools'][0]['label']['title'], 'wrote note.txt')
        # The legacy summary fields are untouched.
        self.assertEqual(summary['tools'][0]['name'], 'write_file')
        self.assertTrue(summary['tools'][0]['ok'])
        self.assertIn('preview', summary['tools'][0])

    def test_an_edit_says_what_changed(self):
        (self.root / 'note.txt').write_text('one\ntwo\n')
        agent, events = self.run_tool('edit_file', {'path': 'note.txt', 'old_string': 'two',
                                                    'new_string': 'three'})
        label = self.of(events, 'tool_result')[0]['label']
        self.assertEqual(label['title'], 'edited note.txt')
        self.assertEqual(label['stats'], ['+1 −1'])
        self.assertTrue(label['inline_diff'])
        self.assertEqual(label['path'], 'note.txt')

    def test_a_failed_call_says_why(self):
        (self.root / 'note.txt').write_text('one\n')
        agent, events = self.run_tool('edit_file', {'path': 'note.txt', 'old_string': 'nope',
                                                    'new_string': 'x'})
        self.assertEqual(self.of(events, 'tool_started'), [])
        label = self.of(events, 'tool_result')[0]['label']
        self.assertFalse(label['ok'])
        self.assertEqual(label['title'], 'edit note.txt')
        self.assertTrue(label['error'].startswith('old_string was not found'))

    def test_the_stored_output_gains_a_detail_and_keeps_everything_else(self):
        agent, events = self.run_tool('run_command', {'command': 'printf hello'})
        turn_id = self.of(events, 'tool_result')[0]['turn_id']
        stored = agent.tool_output(turn_id, 'call-1')
        self.assertEqual(stored['event'], 'tool_output')
        self.assertTrue(stored['stored'])
        self.assertEqual(stored['name'], 'run_command')
        self.assertIn('RUN COMMAND', stored['preview'])
        self.assertEqual(stored['result']['output'], 'hello')
        self.assertTrue(stored['ok'])
        self.assertEqual(stored['exit_code'], 0)
        self.assertEqual(stored['label']['title'], 'ran printf hello')
        self.assertEqual([s['heading'] for s in stored['detail']], ['command', 'output'])
        self.assertEqual(stored['detail'][0]['text'], 'printf hello')
        self.assertEqual(stored['detail'][1]['text'], 'hello')

    def test_the_stored_detail_of_a_write_is_its_diff(self):
        agent, events = self.run_tool('write_file', {'path': 'a.txt', 'content': 'x\n'})
        turn_id = self.of(events, 'tool_result')[0]['turn_id']
        stored = agent.tool_output(turn_id, 'call-1')
        self.assertEqual(stored['detail'][0]['style'], 'diff')
        self.assertIn('+x', stored['detail'][0]['text'])
        self.assertIn('+x', stored['diff'])
