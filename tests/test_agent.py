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
from relay_core.provider import ProviderConfig

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
        self.assertFalse(any('tool_calls' in message for message in agent.messages))

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
