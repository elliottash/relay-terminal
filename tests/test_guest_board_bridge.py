# SPDX-License-Identifier: AGPL-3.0-or-later
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import unittest
from unittest import mock

from relay_core import board as B, board_tools as T
from relay_core.agent import Agent
from relay_core.guest_board_bridge import BOARD_ALLOW, EXEC_ALLOW, REMOTE_ALLOW, TERMINAL_CONTEXT_ALLOW, Bridge, exchange
from relay_core import guest_harness_provider as P
from relay_core.guest_harness import TurnResult
from tests.test_board_tools import CONFIG
from tests.guest_harness_fake import FakeHarness


class BridgeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name) / 'issues'
        root.mkdir()
        (root / B.BOARD_CONFIG).write_text(CONFIG)
        self.board = B.Board(root, Path(self.tmp.name))
        self.events = []
        self.tools = T.BoardTools(self.board, emit=self.events.append,
                                 state_path=Path(self.tmp.name)/'rate.json')
        self.tools.begin_turn('setup')
        self.card = self.tools.run('board_create_card', {
            'tab':'features', 'status':'inbox', 'title':'Bridge test', 'request':'test the bridge'})['id']
        self.harness = FakeHarness()
        with mock.patch.object(P, 'make_harness', return_value=self.harness):
            self.provider = P.start_provider('guest:claude', {}, self.tmp.name)
        self.addCleanup(self.provider.close)
        self.bridge = self.provider.board_bridge
        self.agent = Agent(self.provider.config, self.tmp.name, self.events.append,
                           provider=self.provider, board=self.tools, track_requests=False,
                           completion_check=False, todo_tool=False)
        P.attach(self.agent, self.provider)
        self.cap = {'socket':self.bridge.path, 'token':self.bridge.token}

    def call(self, name='board_read', args=None, key='1'):
        return exchange(self.cap, 'tools/call', {'name':name, 'arguments':args or {'id':self.card}}, key)

    def active(self):
        self.cancel = threading.Event()
        self.bridge.begin(self.cancel)

    def test_discovery_allowlist_and_unavailable(self):
        specs = exchange(self.cap, 'tools/list')['tools']
        self.assertEqual({s['name'] for s in specs}, BOARD_ALLOW | EXEC_ALLOW | TERMINAL_CONTEXT_ALLOW)
        self.assertTrue(all(s['inputSchema']['type']=='object' for s in specs))
        self.assertEqual(self.call()['code'], 'unavailable')
        self.active()
        self.assertEqual(self.call()['code'], 'unavailable')
        self.assertNotIn('error', self.call(key='active'))
        self.assertEqual(self.call('board_create_card', key='2')['code'], 'unknown_tool')
        self.agent.board = None
        self.assertEqual({s['name'] for s in exchange(self.cap, 'tools/list')['tools']}, EXEC_ALLOW | TERMINAL_CONTEXT_ALLOW)

    def test_capability_isolation(self):
        self.active()
        other = Bridge(True)
        self.addCleanup(other.close)
        wrong = dict(self.cap, token=other.token)
        self.assertEqual(exchange(wrong, 'tools/list')['code'], 'unauthorized')

    def test_duplicate_write_once_and_changed_payload_refused(self):
        self.active()
        args = {'id':self.card, 'text':'one mutation', 'kind':'progress'}
        first = self.call('board_comment', args)
        self.assertNotIn('error', first)
        self.assertEqual(first, self.call('board_comment', args))
        self.assertEqual(self.call('board_comment', dict(args,text='different'))['code'], 'request_reused')
        self.assertEqual(self.board.thread_path(self.card).read_text().count('one mutation'),1)

    def test_plan_readonly_stop_and_next_turn(self):
        self.active()
        args = {'id':self.card, 'text':'must not write'}
        self.agent.mode = 'plan'
        # Plan mode locks nothing (#PLDG): the call reaches the board's own validation.
        self.assertNotIn('plan mode', self.call('board_comment',args)['error'])
        self.agent.mode = 'agent'
        self.tools.readonly = True
        self.assertEqual(self.call('board_comment',args,key='readonly')['code'], 'board_readonly_turn')
        self.agent.mode = 'plan'
        self.assertNotIn('error', self.call(key='read'))
        self.cancel.set()
        self.assertEqual(self.call(key='stopped')['code'],'unavailable')
        self.bridge.end()
        self.active()
        self.assertNotIn('error', self.call(key='next'))

    def test_guest_memory_suggestion_reaches_the_pane(self):
        # #MEMS: a guest's suggest is told to the pane as memory_suggested, so its transcript
        # draws Keep / Edit / No; a declined repeat is told too, and a list is not.
        from relay_core import app_tools as A
        self.agent.app = A.AppTools(None, A.AppBridge(self.events.append), workspace=self.tmp.name)
        self.active()
        hq = Path(self.tmp.name) / 'hq'
        with mock.patch.dict('os.environ', {'RELAY_GLOBAL_SWITCHBOARD': str(hq)}):
            first = self.call('app_user_memory', {'action': 'suggest', 'fact': 'Prefers terse answers'}, key='s1')
            self.assertEqual(first.get('status'), 'pending', first)
            told = [e for e in self.events if e.get('event') == 'memory_suggested']
            self.assertEqual(len(told), 1)
            self.assertEqual(told[0]['result']['id'], first['id'])
            self.assertEqual(told[0]['result']['fact'], 'Prefers terse answers')
            self.assertNotIn('rejections', told[0]['result'])
            self.call('app_user_memory', {'action': 'suggestions'}, key='s2')
            self.assertEqual(len([e for e in self.events if e.get('event') == 'memory_suggested']), 1)
            again = self.call('app_user_memory', {'action': 'suggest', 'fact': 'Prefers terse answers'}, key='s3')
            self.assertEqual(again.get('status'), 'duplicate')
            told = [e for e in self.events if e.get('event') == 'memory_suggested']
            self.assertEqual([e['result']['status'] for e in told], ['pending', 'duplicate'])

    def test_provider_turn_binds_native_context_and_revokes_on_failure(self):
        def turn(prompt, attachments, emit, cancel, harness):
            self.assertIn('relay_board', prompt)
            self.agent.config.model = 'claude-live-model'
            result = self.call('board_comment', {'id':self.card,'kind':'progress','text':'native identity'})
            self.assertNotIn('error',result)
            raise RuntimeError('guest failed after write')
        self.harness.script = [turn]
        self.agent.ask('comment on the card')
        self.assertIsNone(self.bridge.active)
        text = self.board.thread_path(self.card).read_text()
        self.assertIn('native identity', text)
        self.assertIn('model=claude-live-model', text)
        self.assertEqual(self.call(key='after')['code'], 'unavailable')

    def test_real_stdio_proxy_initialize_list_call_and_malformed(self):
        self.active()
        proc = subprocess.Popen([self.bridge.descriptor['command'], *self.bridge.descriptor['args']],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True)
        messages = [ {'jsonrpc':'2.0','id':1,'method':'initialize','params':{'protocolVersion':'2025-03-26'}},
                    {'jsonrpc':'2.0','method':'notifications/initialized'},
                    {'jsonrpc':'2.0','id':2,'method':'tools/list'},
                    {'jsonrpc':'2.0','id':3,'method':'tools/call',
                     'params':{'name':'board_read','arguments':{'id':self.card}}}]
        out, err = proc.communicate('\n'.join(json.dumps(m) for m in messages)+'\nnot json\n',timeout=10)
        self.assertEqual(err,'')
        rows = sorted((json.loads(line) for line in out.splitlines()), key=lambda r: r['id'] if r['id'] is not None else 99)
        self.assertEqual(rows[0]['result']['protocolVersion'],'2025-03-26')
        self.assertEqual(len(rows[1]['result']['tools']),len(BOARD_ALLOW | EXEC_ALLOW | TERMINAL_CONTEXT_ALLOW))
        self.assertFalse(rows[2]['result']['isError'])
        self.assertIn('error', rows[3])

    def test_cancel_waiting_call_and_shutdown(self):
        self.active()
        with self.bridge.lock:
            thread = threading.Thread(target=lambda: self.call(key='waiting'))
            thread.start()
            self.cancel.set()
        thread.join(5)
        self.assertFalse(thread.is_alive())
        path = self.bridge.path
        self.provider.close()
        self.assertFalse(Path(path).exists())

    def test_cancelled_request_never_dispatches(self):
        self.active()
        exchange(self.cap, 'cancel', key='cancelled')
        result = self.call('board_comment', {'id':self.card,'kind':'progress','text':'cancel me'}, key='cancelled')
        self.assertEqual(result['code'],'cancelled')
        self.assertNotIn('cancel me', self.board.thread_path(self.card).read_text())

    def test_update_move_gates_and_scope(self):
        self.active()
        current = self.call()['hash']
        result = self.call('board_update_card', {'id':self.card,'base_hash':current,
                           'append_section':{'heading':'Verdict','text':'Bridge integration passed.'}},key='update')
        self.assertNotIn('error',result)
        result = self.call('board_move_card', {'id':self.card,'status':'needs-qa-llm',
                           'reason':'check without evidence'},key='gate')
        self.assertIn('error',result)
        result = self.call('board_move_card', {'id':self.card,'status':'ready',
                           'reason':'Bridge move test'},key='move')
        self.assertNotIn('error',result)
        self.assertEqual(self.board.card_by_id(self.card).status,'ready')
        self.tools.begin_card_turn('plan', self.card)
        result = self.call('board_move_card', {'id':self.card,'status':'done','reason':'no'},key='scope')
        self.assertIn('error',result)

    def test_both_adapter_names_use_native_board_labels_once(self):
        turn = P._Turn(self.provider, self.agent, None, self.events.append, threading.Event())
        for index, source in enumerate([
                {'server':'relay_board','tool':'board_read','arguments':{'id':self.card},'_guest_tool':'mcpToolCall'},
                {'id':self.card,'_guest_tool':'mcp__relay_board__board_read'}]):
            turn._on_tool_started({'call_id':str(index),'tool':'other','input':source})
            turn._on_tool_result({'call_id':str(index),'tool':'other','output':'ok','ok':True})
        events = [e for e in self.events if e.get('event') in ('tool_started','tool_result')]
        self.assertEqual(len(events),4)
        self.assertTrue(all(e['tool']=='board_read' for e in events))

    def test_native_verdict_gate_signature_and_budget(self):
        self.active()
        result = self.call('board_move_card', {'id':self.card,'status':'needs-qa-llm',
                    'reason':'implemented','evidence':'docs/qa_evidence/bridge/',
                    'implemented_by':'spoofed/model'},key='qa')
        self.assertNotIn('error',result)
        card = self.board.card_by_id(self.card)
        self.assertNotEqual(card.front['implemented_by'],'spoofed/model')
        result = self.call('board_move_card', {'id':self.card,'status':'done','reason':'verified'},key='no-verdict')
        self.assertEqual(result['requires'],'verdict')
        current = self.call(key='fresh')['hash']
        result = self.call('board_update_card', {'id':self.card,'base_hash':current,
                    'append_section':{'heading':'Verdict','text':'Passed the integration checks.'}},key='verdict')
        self.assertNotIn('error',result)
        result = self.call('board_move_card', {'id':self.card,'status':'done','reason':'verified'},key='done')
        self.assertNotIn('error',result)
        self.tools.limits['max_writes_per_turn'] = self.tools.writes_this_turn
        result = self.call('board_comment', {'id':self.card,'kind':'progress','text':'over budget'},key='budget')
        self.assertEqual(result['code'],'board_rate_limited')

    def test_policy_prefers_available_tools_and_preserves_fallback(self):
        text = B.policy_text(self.board)
        self.assertIn('relay_board', text)
        self.assertIn('Without the board tools', text)

    def test_remote_schemas_stable_and_never_fall_back_to_local(self):
        self.active()
        before = self.bridge.specs()
        for spec in before:
            if spec['name'] in REMOTE_ALLOW:
                self.assertIn('host', spec['inputSchema']['required'])
        result = self.call('run_command', {'command':'echo must-not-run'}, key='nohost')
        self.assertIn('explicit active SSH host', result['error'])
        result = self.call('run_command', {'command':'echo must-not-run','host':'box'}, key='noremote')
        self.assertIn('not logged into any host', result['error'])
        self.agent.executor.set_remote_session({'host':'box','reachable':False})
        self.assertEqual(before, self.bridge.specs())
        result = self.call('run_command', {'command':'echo must-not-run','host':'other'}, key='wrong')
        self.assertIn('not the host', result['error'])
        result = self.call('run_command', {'command':'echo must-not-run','host':'box'}, key='unshared')
        self.assertIn("can't be shared", result['error'])

    def test_terminal_capability_checked_per_turn(self):
        self.active()
        args={'command':'echo test','mode':'prefill','intent':'Test terminal bridge'}
        result=self.call('run_in_terminal',args,key='not-offered')
        self.assertIn('error',result)
        self.agent.executor.terminal.begin_turn('prefill')
        with mock.patch.object(self.agent.executor.terminal, 'execute', return_value={'ok':True,'action':'prefilled'}) as run:
            result=self.call('run_in_terminal',args,key='offered')
        self.assertTrue(result['ok']); run.assert_called_once()
        self.agent.executor.terminal.end_turn()
        self.assertIn('error',self.call('run_in_terminal',args,key='revoked'))

    def test_both_adapter_names_preserve_remote_tool_and_host(self):
        turn=P._Turn(self.provider,self.agent,None,self.events.append,threading.Event())
        for source in ({'server':'relay_board','tool':'run_command','arguments':{'command':'pwd','host':'box'},'_guest_tool':'mcpToolCall'},
                       {'command':'pwd','host':'box','_guest_tool':'mcp__relay_board__run_command'}):
            self.assertEqual(turn._tool_name({'tool':'other','input':source})[0],'run_command')
