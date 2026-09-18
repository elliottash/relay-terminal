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
from relay_core.provider import ProviderConfig, ProviderStalled

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
