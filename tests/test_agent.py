import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import types
import unittest
from pathlib import Path
from unittest import mock

from relay_core import board as B
from relay_core import board_tools as T

from relay_core import agent as agent_module
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

    def test_guard_refusal_grade_survives_events_storage_and_model_messages(self):
        for stage in ("_prepare", "_execute"):
            for error, refused in ((ValueError("guard refused"), True),
                                   (OSError("disk failed"), False),
                                   (UnicodeDecodeError("utf8", b"\xff", 0, 1, "invalid"), False)):
                with self.subTest(stage=stage, error=type(error).__name__):
                    events = []
                    fake = FakeProvider(tool("run_command", {"command": "true"}))
                    agent = Agent(CONFIG, self.temp.name, events.append, provider=fake)
                    with mock.patch.object(agent, stage, side_effect=error):
                        agent.ask("do it")
                    event = next(e for e in events if e["event"] == "tool_result")
                    summary = next(e for e in events if e["event"] == "turn_summary")
                    stored = agent.tool_output(event["turn_id"], "call-1")
                    for label in (event["label"], summary["tools"][0]["label"], stored["label"]):
                        self.assertFalse(label["ok"])
                        self.assertEqual(label.get("refused", False), refused)
                    message = json.loads(next(m["content"] for m in fake.messages if m["role"] == "tool"))
                    self.assertEqual(message.get("refused", False), refused)
                    self.assertEqual(event["result"], message)

    def test_tool_budget_is_a_refusal(self):
        response = tool("run_command", {"command": "true"})
        second = json.loads(json.dumps(response["tool_calls"][0]))
        second["id"] = "call-2"
        response["tool_calls"].append(second)
        events = []
        agent = Agent(CONFIG, self.temp.name, events.append,
                      provider=FakeProvider(response), max_tool_calls=1)
        agent.ask("do it")
        results = [e for e in events if e["event"] == "tool_result"]
        self.assertTrue(results[1]["result"]["refused"])
        self.assertTrue(results[1]["label"]["refused"])
        self.assertFalse(results[1]["label"]["ok"])

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

    def test_reset_conversation_emits_the_fresh_context(self):
        # Issue 5PY9: /new left the context chip on the previous conversation's percentage,
        # because reset_conversation rebuilt the session without ever emitting a context event.
        # Fresh still means the system prompt and tool schemas, so it is a few thousand tokens
        # (a few percent of the window), never zero.
        events = []
        fake = FakeProvider(tool('run_command', {'command': 'printf HELLO'}))
        agent = Agent(CONFIG, self.temp.name, events.append, provider=fake)
        agent.ask('run something')
        during = [e for e in events if e.get('event') == 'context']
        self.assertTrue(during)
        used = during[-1]['used_tokens']
        agent.reset_conversation()
        after = [e for e in events if e.get('event') == 'context']
        self.assertEqual(len(after), len(during) + 1)   # the reset emits exactly one
        fresh = after[-1]
        self.assertLess(fresh['used_tokens'], used)
        self.assertLess(fresh['percent'], 10.0)
        self.assertEqual(fresh['window'], during[-1]['window'])

class SystemPromptTests(unittest.TestCase):
    def test_replies_are_asked_for_in_markdown(self):
        # The GUI renders agent replies as Markdown in the terminal (src/MarkdownAnsi.cpp).
        from relay_core import agent as agent_module
        self.assertIn("Format replies as Markdown", agent_module.SYSTEM)
        self.assertIn("fenced code blocks", agent_module.SYSTEM)
        # The main point is asked for in bold, and the terminal colours three labels (card #CVHT).
        self.assertIn("Lead with the main point in bold", agent_module.SYSTEM)
        for label in ("**Done:**", "**Problem:**", "**Need:**"):
            self.assertIn(label, agent_module.SYSTEM)


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

    def test_the_first_token_may_be_given_longer_than_the_gaps_between_chunks(self):
        """Options › "Wait longer for the first token": prefill on a big prompt is not a stall."""
        provider = self.StallingProvider(stalls=0)
        agent = self.agent(provider, [])
        self.assertEqual(agent.options()['first_token_timeout_s'], 0.0)      # off: one deadline
        self.assertEqual(agent.set_options({'first_token_timeout_s': 240})['first_token_timeout_s'], 240.0)
        self.assertEqual(agent.set_options({'first_token_timeout_s': 0})['first_token_timeout_s'], 0.0)
        with self.assertRaises(ValueError):
            agent.set_options({'first_token_timeout_s': 3600})


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

    def test_a_label_bug_cannot_end_the_turn(self):
        # #0CJY: pane 6dbee2a7's turn died on a NameError inside tool_labels.started_label,
        # before the tool even ran. A label is presentation: the call happens on a plain-name
        # label, the ✓/✗ stays honest, and the turn finishes.
        import relay_core.tool_labels as labels
        def boom(*args, **kwargs):
            raise NameError('result')
        started, finished = labels.started_label, labels.result_label
        labels.started_label, labels.result_label = boom, boom
        try:
            agent, events = self.run_tool('run_command', {'command': 'echo hi'})
        finally:
            labels.started_label, labels.result_label = started, finished
        self.assertEqual(self.of(events, 'tool_started')[0]['label'],
                         {'kind': 'tool', 'running': 'run_command', 'title': 'run_command'})
        result = self.of(events, 'tool_result')[0]
        self.assertEqual(result['label'],
                         {'kind': 'tool', 'running': 'run_command', 'title': 'run_command',
                          'ok': True})
        self.assertEqual(result['result']['exit_code'], 0)
        # The turn reached its summary: the model answered after the call, nothing ended early.
        summary = self.of(events, 'turn_summary')[0]
        self.assertEqual(summary['tools'][0]['name'], 'run_command')
        self.assertTrue(summary['tools'][0]['ok'])

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


# ----- uncapped turns, loop detection, recitation (card #2CZP) ------------------------------------

def call_of(name, arguments, cid):
    return {'id': cid, 'type': 'function', 'function': {'name': name, 'arguments': json.dumps(arguments)}}


class ScriptedProvider:
    """Answers with whatever `plan(n)` returns for the n-th call: a message, or None to finish."""
    def __init__(self, plan): self.plan=plan; self.calls=0
    def complete(self, messages, tools, emit, cancel):
        self.calls += 1
        message = self.plan(self.calls)
        if message is not None:
            return message
        emit({'event':'delta','text':'Finished.'})
        return {'role':'assistant','content':'Finished.'}
    def cancel(self): pass


class StubRoles:
    """Enough of a RoleResolver for role_model(); side_provider short-circuits to the fake provider."""
    def resolve(self, role):
        return types.SimpleNamespace(is_main=True, config=types.SimpleNamespace(model='lite-model'))


class LoopAndRecitationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.root=Path(self.temp.name)
    def tearDown(self): self.temp.cleanup()

    def of(self, events, name):
        return [e for e in events if e['event'] == name]

    def repeating(self, path='missing.txt'):
        """The same failing read, for ever: the `error` pattern at three calls in a row."""
        return ScriptedProvider(lambda n: {'role':'assistant','content':'',
                                           'tool_calls':[call_of('read_file', {'path': path}, f'c{n}')]})

    def walking(self, steps, finish_at=None):
        """A different file each step, which is a batch operation and must never look like a loop."""
        def plan(n):
            if finish_at is not None and n > finish_at:
                return None
            return {'role':'assistant','content':'',
                    'tool_calls':[call_of('read_file', {'path': f'f{n}.txt'}, f'c{n}')]}
        for i in range(steps + 2):
            (self.root / f'f{i + 1}.txt').write_text('x\n')
        return ScriptedProvider(plan)

    def test_the_defaults_are_the_clamp_maxima_not_a_working_limit(self):
        # Owner, 2026-09-20: uncapped by default. Both sit at validate_turn_options' maxima, so the
        # settings are a backstop fuse for a runaway turn rather than the normal stop.
        from relay_core.agent import DEFAULT_MAX_STEPS, DEFAULT_MAX_TOOL_CALLS
        self.assertEqual((DEFAULT_MAX_STEPS, DEFAULT_MAX_TOOL_CALLS), (500, 2000))
        agent = Agent(CONFIG, self.temp.name, lambda e: None, provider=self.repeating())
        self.assertEqual((agent.options()['max_steps'], agent.options()['max_tool_calls']), (500, 2000))

    def test_a_long_turn_runs_past_the_old_step_and_tool_call_limits(self):
        # The old caps were 256 steps and 150 tool calls; on the defaults neither ends this turn.
        events=[]
        fake=self.walking(300, finish_at=300)
        agent=Agent(CONFIG, self.temp.name, events.append, provider=fake, context_window=2_000_000)
        agent.ask('read all of them')
        self.assertGreater(fake.calls, 256)
        self.assertGreater(len(self.of(events, 'tool_result')), 150)
        self.assertEqual(events[-1]['event'], 'done')
        self.assertNotIn('stop_reason', events[-1])
        self.assertEqual(self.of(events, 'loop_detected'), [])   # a batch of distinct reads is not a loop

    def test_a_repeated_failing_call_is_nudged_twice_and_then_stops_the_turn(self):
        events=[]
        agent=Agent(CONFIG, self.temp.name, events.append, provider=self.repeating())
        agent.ask('read it')
        nudges=self.of(events, 'loop_detected')
        self.assertEqual([e['nudge'] for e in nudges], [1, 2, 3])
        self.assertEqual([e['stopping'] for e in nudges], [False, False, True])
        self.assertEqual(nudges[0]['pattern'], 'error')
        self.assertEqual(nudges[0]['tool'], 'read_file')
        # The nudges are user-role notes naming what repeated, injected at a step boundary — never
        # between an assistant's tool calls and their results, which no provider would accept.
        notes=[m for m in agent.messages if m.get('relay_kind') == 'note' and 'not making progress' in m['content']]
        self.assertEqual(len(notes), 2)
        for index, message in enumerate(agent.messages):
            if message.get('relay_kind') == 'note' and index:
                self.assertFalse(agent.messages[index - 1].get('tool_calls'))
        done=events[-1]
        self.assertEqual((done['event'], done['stop_reason']), ('done', 'limit'))
        self.assertEqual(done['limit']['which'], 'loop')
        self.assertEqual((done['limit']['pattern'], done['limit']['tool'], done['limit']['nudges']),
                         ('error', 'read_file', 2))
        # Nowhere near the count limits: this stop is the detector's, and it says so.
        self.assertLess(done['limit']['steps'], done['limit']['max_steps'])
        self.assertIn('read_file', done['text'])

    def test_a_productive_verdict_from_the_double_check_clears_the_detector(self):
        # Gemini-style second opinion: the deterministic pattern stands unless a Lite model says the
        # repetition is productive, and then the turn simply carries on.
        events=[]
        agent=Agent(CONFIG, self.temp.name, events.append, provider=self.repeating(), roles=StubRoles(),
                    max_steps=12)
        with mock.patch.object(agent_module.loopdetect, 'run_check', return_value=False) as checked:
            agent.ask('read it')
        self.assertTrue(checked.called)
        self.assertEqual(self.of(events, 'loop_detected'), [])
        self.assertEqual([e['verdict'] for e in self.of(events, 'loop_check')][:1], ['productive'])
        self.assertEqual(self.of(events, 'loop_check')[0]['model'], 'lite-model')
        # It ran to the configured backstop instead, which is what the fuse is for.
        self.assertEqual(events[-1]['limit']['which'], 'steps')

    def test_a_loop_verdict_confirms_the_pattern_and_a_failing_check_does_not_block_the_turn(self):
        for name, check in (('confirmed', mock.Mock(return_value=True)),
                            ('failed', mock.Mock(side_effect=ValueError('no key'))),
                            ('unreadable', mock.Mock(return_value=None))):
            with self.subTest(name):
                events=[]
                agent=Agent(CONFIG, self.temp.name, events.append, provider=self.repeating(),
                            roles=StubRoles())
                with mock.patch.object(agent_module.loopdetect, 'run_check', check):
                    agent.ask('read it')
                self.assertEqual(len(self.of(events, 'loop_detected')), 3)
                self.assertEqual(events[-1]['limit']['which'], 'loop')
                checks=self.of(events, 'loop_check')
                self.assertEqual(checks[0]['verdict'], 'loop' if name == 'confirmed' else 'unknown')
                if name == 'failed':
                    self.assertEqual(checks[0]['error'], 'no key')

    def test_the_cadence_reminder_arrives_on_the_step_mark_and_not_before(self):
        # 25 model steps or 50 tool calls, whichever comes first. One call per step reaches the step
        # mark first, so that is the one this run proves; the tool-call mark is the test below.
        events=[]
        fake=self.walking(60, finish_at=60)
        agent=Agent(CONFIG, self.temp.name, events.append, provider=fake, context_window=2_000_000)
        # Every call counts as a long wait, so the cadence alone decides when (card #VQXA).
        with mock.patch.object(agent_module, 'LONG_TOOL_WAIT_S', 0.0):
            agent.ask('read all of them and tell me what changed')
        recited=self.of(events, 'recitation')
        self.assertEqual([e['steps'] for e in recited], [25, 50])
        notes=[m for m in agent.messages if m.get('relay_kind') == 'recitation']
        self.assertEqual(len(notes), len(recited))
        # It is built from what Relay already holds: the ask, the open items, the recent calls.
        self.assertIn('read all of them and tell me what changed', notes[0]['content'])
        self.assertIn('Still open', notes[0]['content'])
        self.assertIn('read_file', notes[0]['content'])
        # Its relay_kind is its own, so compaction and the transcript can tell it from a reminder.
        self.assertNotIn('recitation', [m.get('relay_kind') for m in agent.messages
                                        if 'not making progress' in (m.get('content') or '')])

    def test_a_step_that_calls_many_tools_reaches_the_tool_call_mark_first(self):
        # The other half of "whichever comes first": ten calls a step reaches 50 tool calls at step
        # five, long before the step mark, which is the shape a parallel batch of writes has.
        events=[]
        for i in range(120):
            (self.root / f'f{i}.txt').write_text('x\n')
        def plan(n):
            if n > 8:
                return None
            return {'role':'assistant','content':'',
                    'tool_calls':[call_of('read_file', {'path': f'f{n * 10 + i}.txt'}, f'c{n}-{i}')
                                  for i in range(10)]}
        agent=Agent(CONFIG, self.temp.name, events.append, provider=ScriptedProvider(plan),
                    context_window=2_000_000)
        # Every call counts as a long wait, so the cadence alone decides when (card #VQXA).
        with mock.patch.object(agent_module, 'LONG_TOOL_WAIT_S', 0.0):
            agent.ask('read them all in batches and report')
        recited=self.of(events, 'recitation')
        self.assertTrue(recited, 'a turn past 50 tool calls recites what is still open')
        self.assertLess(recited[0]['steps'], 25)
        self.assertGreaterEqual(recited[0]['tool_calls'], 50)

    def test_a_short_turn_and_a_subagent_are_never_recited_to(self):
        events=[]
        agent=Agent(CONFIG, self.temp.name, events.append, provider=self.walking(3, finish_at=3),
                    context_window=2_000_000)
        agent.ask('read three files')
        self.assertEqual(self.of(events, 'recitation'), [])
        # A subagent keeps no request ledger of its own (track_requests off), so there is nothing to
        # recite and the cadence stays silent however long it runs.
        events=[]
        sub=Agent(CONFIG, self.temp.name, events.append, provider=self.walking(60, finish_at=60),
                  track_requests=False, context_window=2_000_000)
        sub.ask('read all of them')
        self.assertEqual(self.of(events, 'recitation'), [])
        self.assertEqual([m for m in sub.messages if m.get('relay_kind') == 'recitation'], [])

    def test_the_cadence_alone_recites_nothing(self):
        # Card #VQXA: 60 quick steps with no compaction, steer, takeover or slow tool. The recital
        # is due at 25 and 50, but nothing happened that could have displaced the ask.
        events=[]
        agent=Agent(CONFIG, self.temp.name, events.append, provider=self.walking(60, finish_at=60),
                    context_window=2_000_000)
        agent.ask('read all of them and tell me what changed')
        self.assertEqual(self.of(events, 'recitation'), [])


class GuestAggregateUsageTests(unittest.TestCase):
    """#CP3M: a guest harness reports the turn's aggregate (every request it made, summed),
    which is a usage total — never one request's prompt, so never the context measurement."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.root = Path(self.temp.name)
    def tearDown(self): self.temp.cleanup()

    def test_guest_turn_aggregate_never_becomes_the_context_measurement(self):
        class GuestProvider:
            serves_side_calls = False
            def complete(self, messages, tools, emit, cancel):
                emit({'event':'usage','usage':{'prompt_tokens':3_100_000,'completion_tokens':5_465,
                                               'total_tokens':3_105_465,
                                               'guest_context_tokens':181_959,
                                               'guest_context_window':258_400}})
                emit({'event':'delta','text':'Done.'})
                return {'role':'assistant','content':'Done.'}
            def cancel(self): pass
        events=[]
        agent = Agent(CONFIG, self.temp.name, events.append, provider=GuestProvider())
        agent.ask('do it')
        # The aggregate still counts toward the session's usage totals…
        self.assertEqual(agent.usage_totals['total_tokens'], 3_105_465)
        # …but the tracker never took it as one request's prompt: no recorded measurement,
        # no 4x-calibrated estimate ratio, and `used` stays an estimate of the transcript.
        self.assertIsNone(agent.context._usage_tokens)
        self.assertEqual(agent.context.ratio, 1.0)
        used, estimated = agent.context.used(agent.messages, [])
        self.assertTrue(estimated)
        self.assertLess(used, 10_000)

    def test_relay_provider_usage_still_calibrates_the_tracker(self):
        class UsageProvider:
            def complete(self, messages, tools, emit, cancel):
                emit({'event':'usage','usage':{'prompt_tokens':5_000,'completion_tokens':100,
                                               'total_tokens':5_100}})
                emit({'event':'delta','text':'Done.'})
                return {'role':'assistant','content':'Done.'}
            def cancel(self): pass
        events=[]
        agent = Agent(CONFIG, self.temp.name, events.append, provider=UsageProvider())
        agent.ask('do it')
        self.assertEqual(agent.context._usage_tokens, 5_100)

    def test_unchanged_transcript_reports_no_reduction(self):
        class UsageProvider:
            def complete(self, messages, tools, emit, cancel):
                emit({'event':'usage','usage':{'prompt_tokens':5_000,'completion_tokens':100,
                                               'total_tokens':5_100}})
                emit({'event':'delta','text':'Done.'})
                return {'role':'assistant','content':'Done.'}
            def cancel(self): pass
        events=[]
        agent = Agent(CONFIG, self.temp.name, events.append, provider=UsageProvider())
        agent.ask('one short turn')
        before_messages = [dict(m) for m in agent.messages]
        event = agent.compact('manual')
        self.assertEqual(event['summary_chars'], 0)
        self.assertEqual(event['trimmed_tool_outputs'], 0)
        self.assertEqual(event['before_tokens'], event['after_tokens'])
        self.assertEqual(agent.messages, before_messages)
        # The tracker was not invalidated: the usage measurement is still the basis.
        self.assertEqual(agent.context._usage_tokens, 5_100)


# A board small enough for one test, and a land.py fake that records its own argv next to
# itself: the board-sync of a turn (#FYEY) is observed through exactly what it was handed.
BOARD_SYNC_CONFIG = """\
version: 1
tabs: [{id: features, folder: features}, {id: done, filter: "status:done,dropped"}]
columns: [inbox, executing, needs-verification, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
"""

FAKE_LAND = """\
import json, os, sys
log = os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), "land-calls.jsonl")
with open(log, "a") as fh:
    fh.write(json.dumps(sys.argv) + "\\n")
print("nothing to land")
"""


def board_call(call_id, name, arguments):
    return {"id": call_id, "type": "function",
            "function": {"name": name, "arguments": json.dumps(arguments)}}


class BoardSyncTests(unittest.TestCase):
    """The turn's board writes land per turn (card #FYEY, decision 6): `_end_turn` runs
    `scripts/land.py board-sync <token> -m "board: <pane> <turn>" <paths>` in a thread of its
    own, exactly once, with exactly the board paths the turn wrote — and never when the turn
    wrote none or the repo has no land script."""

    pane_token = "0f0e3d1c-3b1b-4d3f-9a52-6a1f6f5f0000"

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name).resolve()
        (self.root / "issues").mkdir()
        (self.root / "issues" / "board.yaml").write_text(BOARD_SYNC_CONFIG, encoding="utf-8")
        self.board_tools = T.BoardTools(
            B.Board(self.root / "issues", self.root),
            emit=lambda event: None,
            context=T.ToolContext(actor="agent", model="anthropic/claude-opus-5-5", pane="2"),
            state_path=self.root / ".relay" / "board-rate.json",
            pane_token=self.pane_token)

    def tearDown(self):
        self.tmp.cleanup()

    def land_script(self):
        scripts = self.root / "scripts"
        scripts.mkdir(exist_ok=True)
        (scripts / "land.py").write_text(FAKE_LAND, encoding="utf-8")

    def calls(self):
        log = self.root / "scripts" / "land-calls.jsonl"
        return [json.loads(line) for line in
                log.read_text(encoding="utf-8").splitlines()] if log.exists() else []

    def ask(self, response):
        events = []
        agent = Agent(CONFIG, str(self.root), events.append, provider=FakeProvider(response),
                      board=self.board_tools)
        agent.ask("work on the board")
        return events

    def settle(self, count, seconds=5):
        """Wait for the background sync (or its absence) to settle."""
        deadline = time.time() + seconds
        while time.time() < deadline:
            if len(self.calls()) >= count:
                break
            time.sleep(0.02)
        return self.calls()

    def test_a_turn_that_wrote_two_cards_syncs_them_once(self):
        self.land_script()
        response = {"role": "assistant", "content": "", "reasoning_content": "",
                    "tool_calls": [
                        board_call("c1", "board_create_card",
                                   {"tab": "features", "status": "inbox", "title": "One",
                                    "request": "the first card"}),
                        board_call("c2", "board_create_card",
                                   {"tab": "features", "status": "inbox", "title": "Two",
                                    "request": "the second card"})]}
        events = self.ask(response)
        self.assertEqual(events[-1]["event"], "done", events[-1])
        calls = self.settle(1)
        self.assertEqual(len(calls), 1, calls)
        argv = calls[0]
        self.assertEqual(argv[1:3], ["board-sync", self.pane_token])
        self.assertEqual(argv[3], "-m")
        message = argv[4]
        self.assertTrue(message.startswith("board: 2 "), message)
        self.assertNotEqual(message.split()[-1], "2", "the turn id is missing from the message")
        paths = argv[5:]
        cards = [p for p in paths if p.startswith("issues/features/") and "/threads/" not in p]
        self.assertEqual(len(cards), 2, paths)
        self.assertEqual(len(set(paths)), len(paths), "a path was synced twice")

    def test_a_turn_without_board_writes_calls_nothing(self):
        self.land_script()
        events = self.ask(tool("run_command", {"command": "printf HELLO"}))
        self.assertEqual(events[-1]["event"], "done", events[-1])
        self.settle(0, seconds=1)
        self.assertEqual(self.calls(), [])

    def test_a_repo_without_a_land_script_calls_nothing(self):
        events = self.ask({"role": "assistant", "content": "", "reasoning_content": "",
                           "tool_calls": [
                               board_call("c1", "board_create_card",
                                          {"tab": "features", "status": "inbox", "title": "One",
                                           "request": "the first card"})]})
        self.assertEqual(events[-1]["event"], "done", events[-1])
        self.settle(0, seconds=1)
        self.assertFalse((self.root / "scripts").exists())
