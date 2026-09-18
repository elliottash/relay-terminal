import json
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

from relay_core.agent import Agent
from relay_core.provider import Cancelled, ProviderConfig
from relay_core.queue import MAX_QUEUE, TurnSupervisor

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ProviderConfig('http://127.0.0.1:12345/v1', 'mock', '')


class Recorder:
    def __init__(self):
        self.events, self.cond = [], threading.Condition()
        self.hook = None

    def __call__(self, event):
        with self.cond:
            self.events.append(event); self.cond.notify_all()
        if self.hook:
            self.hook(event)

    def wait(self, pred, timeout=5.0):
        deadline = time.monotonic() + timeout
        with self.cond:
            while True:
                for e in self.events:
                    if pred(e):
                        return e
                left = deadline - time.monotonic()
                if left <= 0:
                    raise AssertionError('timed out; events: ' + json.dumps(self.events)[-2000:])
                self.cond.wait(left)

    def of(self, kind):
        with self.cond:
            return [e for e in self.events if e['event'] == kind]


class GatedProvider:
    """Each turn streams a delta then blocks until released or cancelled."""
    def __init__(self, cancel_delay=0.0):
        self.prompts, self.active, self.max_active = [], 0, 0
        self.release = threading.Semaphore(0)
        self.cancel_delay = cancel_delay
        self.lock = threading.Lock()

    def complete(self, messages, tools, emit, cancel):
        prompt = messages[-1]['content']
        with self.lock:
            self.active += 1; self.max_active = max(self.max_active, self.active); self.prompts.append(prompt)
        try:
            emit({'event': 'delta', 'text': 'working on ' + prompt})
            while not self.release.acquire(timeout=0.02):
                if cancel.is_set():
                    time.sleep(self.cancel_delay)  # stalled network read
                    raise Cancelled('Stopped.')
            return {'role': 'assistant', 'content': 'finished ' + prompt}
        finally:
            with self.lock:
                self.active -= 1

    def cancel(self): pass


class ToolThenFinish:
    def __init__(self, arguments):
        self.arguments, self.calls = arguments, 0

    def complete(self, messages, tools, emit, cancel):
        self.calls += 1
        if messages[-1]['role'] == 'user' and 'tool please' in messages[-1]['content']:
            return {'role': 'assistant', 'content': '', 'tool_calls': [
                {'id': 'c1', 'type': 'function', 'function': {'name': 'run_command', 'arguments': json.dumps(self.arguments)}}]}
        return {'role': 'assistant', 'content': 'plain answer'}

    def cancel(self): pass


class SupervisorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.rec = Recorder()
        self.sup = TurnSupervisor(self.rec)

    def tearDown(self):
        self.sup.shutdown(timeout=3)
        self.temp.cleanup()

    def use(self, provider):
        self.agent = Agent(CONFIG, self.temp.name, self.sup.agent_emit, provider=provider)
        self.sup.set_agent(self.agent)
        return provider

    def finished(self, item_id, timeout=5.0):
        return self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] == item_id, timeout)

    def test_queue_runs_in_order_without_overlap(self):
        p = self.use(GatedProvider())
        ids = [self.sup.submit('first', 'queue'), self.sup.submit('second', 'queue'), self.sup.submit('third', 'queue')]
        positions = [e['position'] for e in self.rec.of('queued')]
        self.assertEqual(positions, [0, 1, 2])
        for _ in ids:
            p.release.release()
        for i in ids:
            self.assertEqual(self.finished(i)['outcome'], 'done')
        self.assertEqual(p.prompts, ['first', 'second', 'third'])
        self.assertEqual(p.max_active, 1)
        self.assertEqual([e['id'] for e in self.rec.of('agent_started')], ids)

    def test_now_is_refused_while_busy(self):
        p = self.use(GatedProvider())
        first = self.sup.submit('first', 'now')
        self.rec.wait(lambda e: e['event'] == 'agent_started')
        with self.assertRaisesRegex(ValueError, 'already active'):
            self.sup.submit('second', 'now')
        p.release.release(); self.finished(first)

    def test_remove_and_clear(self):
        p = self.use(GatedProvider())
        first = self.sup.submit('first', 'queue')
        self.rec.wait(lambda e: e['event'] == 'agent_started')
        second = self.sup.submit('second', 'queue'); third = self.sup.submit('third', 'queue')
        self.sup.remove(second)
        with self.assertRaises(ValueError):
            self.sup.remove(second)
        with self.assertRaises(ValueError):
            self.sup.remove(first)  # already running
        self.assertEqual([i['id'] for i in self.rec.of('queue_changed')[-1]['items']], [third])
        self.sup.clear()
        self.assertEqual(self.rec.of('queue_changed')[-1]['items'], [])
        p.release.release(); self.finished(first)
        time.sleep(0.1)
        self.assertEqual(p.prompts, ['first'])

    def test_interrupt_while_streaming_runs_new_prompt_next(self):
        p = self.use(GatedProvider(cancel_delay=0.5))  # old turn is slow to exit
        first = self.sup.submit('first', 'now')
        self.rec.wait(lambda e: e.get('text') == 'working on first')
        queued = self.sup.submit('queued later', 'queue')
        urgent = self.sup.submit('urgent', 'interrupt')
        self.assertEqual(self.rec.of('interrupting')[-1], {'event': 'interrupting', 'id': first, 'by': urgent})
        self.assertEqual(self.finished(first)['outcome'], 'cancelled')
        self.rec.wait(lambda e: e.get('text') == 'working on urgent')
        self.assertEqual(p.max_active, 1)
        p.release.release(); self.assertEqual(self.finished(urgent)['outcome'], 'done')
        p.release.release(); self.assertEqual(self.finished(queued)['outcome'], 'done')
        self.assertEqual(p.prompts, ['first', 'urgent', 'queued later'])
        # Cancelled turn left no dangling tool group; history is coherent for the provider.
        self.assertFalse(any('tool_calls' in m for m in self.agent.messages))

    def test_interrupt_when_idle_starts_immediately(self):
        p = self.use(GatedProvider())
        item = self.sup.submit('hello', 'interrupt')
        self.rec.wait(lambda e: e['event'] == 'agent_started' and e['id'] == item)
        self.assertEqual(self.rec.of('interrupting'), [])
        p.release.release(); self.finished(item)

    def test_multiple_interrupts_are_fifo(self):
        p = self.use(GatedProvider(cancel_delay=0.3))
        first = self.sup.submit('first', 'now')
        self.rec.wait(lambda e: e.get('text') == 'working on first')
        a = self.sup.submit('a', 'interrupt'); b = self.sup.submit('b', 'interrupt')
        self.finished(first)
        self.rec.wait(lambda e: e.get('text') == 'working on a')
        p.release.release(); self.finished(a)
        p.release.release(); self.finished(b)
        self.assertEqual(p.prompts, ['first', 'a', 'b'])

    def test_interrupt_while_tool_command_runs(self):
        self.use(ToolThenFinish({'command': 'sleep 20; touch late', 'timeout_seconds': 60}))
        first = self.sup.submit('tool please', 'now')
        self.rec.wait(lambda e: e['event'] == 'tool_started')
        time.sleep(0.2)
        start = time.monotonic()
        second = self.sup.submit('next', 'interrupt')
        self.finished(first, timeout=5)
        self.assertEqual(self.finished(second, timeout=5)['outcome'], 'done')
        self.assertLess(time.monotonic() - start, 5)
        self.assertFalse((Path(self.temp.name) / 'late').exists())

    def test_cancel_pauses_queue_until_resume(self):
        p = self.use(GatedProvider())
        first = self.sup.submit('first', 'now')
        self.rec.wait(lambda e: e['event'] == 'agent_started')
        second = self.sup.submit('second', 'queue')
        self.sup.cancel()
        self.assertEqual(self.finished(first)['outcome'], 'cancelled')
        self.assertTrue(self.rec.of('queue_changed')[-1]['paused'])
        time.sleep(0.2)
        self.assertEqual(p.prompts, ['first'])
        self.sup.resume()
        p.release.release()
        self.assertEqual(self.finished(second)['outcome'], 'done')
        self.assertFalse(self.rec.of('queue_changed')[-1]['paused'])

    def test_now_runs_while_queue_paused(self):
        p = self.use(GatedProvider())
        first = self.sup.submit('first', 'now')
        self.rec.wait(lambda e: e['event'] == 'agent_started')
        waiting = self.sup.submit('waiting', 'queue')
        self.sup.cancel(); self.finished(first)
        direct = self.sup.submit('direct', 'now')
        p.release.release(); self.finished(direct)
        time.sleep(0.1)
        self.assertEqual(p.prompts, ['first', 'direct'])
        self.assertTrue(self.rec.of('queue_changed')[-1]['paused'])
        self.sup.remove(waiting)
        self.assertFalse(self.rec.of('queue_changed')[-1]['paused'])

    def test_cancel_drops_pending_interrupt(self):
        p = self.use(GatedProvider(cancel_delay=0.3))
        first = self.sup.submit('first', 'now')
        self.rec.wait(lambda e: e.get('text') == 'working on first')
        self.sup.submit('urgent', 'interrupt')
        self.sup.cancel()
        self.finished(first); time.sleep(0.2)
        self.assertEqual(p.prompts, ['first'])

    def test_configure_and_reset_refused_while_running_and_clear_queue_when_idle(self):
        p = self.use(GatedProvider())
        first = self.sup.submit('first', 'now')
        self.rec.wait(lambda e: e['event'] == 'agent_started')
        self.sup.submit('second', 'queue')
        with self.assertRaises(ValueError): self.sup.reset()
        with self.assertRaises(ValueError): self.sup.set_agent(self.agent)
        self.sup.cancel(); self.finished(first)
        self.sup.reset()
        self.assertEqual(self.rec.of('queue_changed')[-1]['items'], [])
        self.assertEqual(len(self.agent.messages), 1)

    def test_validation(self):
        with self.assertRaisesRegex(ValueError, 'Configure'):
            self.sup.submit('x', 'queue')
        p = self.use(GatedProvider())
        with self.assertRaises(ValueError): self.sup.submit('x', 'later')
        with self.assertRaises(ValueError): self.sup.submit('   ', 'queue')
        with self.assertRaises(ValueError): self.sup.submit(123, 'queue')
        self.sup.submit('running', 'now')
        self.rec.wait(lambda e: e['event'] == 'agent_started')
        for i in range(MAX_QUEUE):
            self.sup.submit(f'q{i}', 'queue')
        with self.assertRaisesRegex(ValueError, 'full'):
            self.sup.submit('overflow', 'queue')
        self.sup.clear(); self.sup.cancel()


class WorkerQueueProtocolTests(unittest.TestCase):
    def run_worker(self, messages, timeout=10):
        proc = subprocess.run([sys.executable, '-S', str(ROOT / 'backend/worker.py')],
                              input=''.join(json.dumps(m) + '\n' for m in messages),
                              text=True, capture_output=True, timeout=timeout, cwd=ROOT)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        return [json.loads(line) for line in proc.stdout.splitlines()]

    def test_protocol_errors(self):
        results = self.run_worker([
            {'type': 'ask', 'id': 'r0', 'text': 'hi', 'when': 'queue'},
            {'type': 'configure', 'base_url': 'http://127.0.0.1:1/v1', 'model': 'm', 'api_key': '', 'workspace': str(ROOT)},
            {'type': 'ask', 'id': 'r1', 'text': 'hi', 'when': 'sometime'},
            {'type': 'queue_remove', 'id': 'r2', 'item': 'nope'},
            {'type': 'shutdown'}])
        errors = [e for e in results if e['event'] == 'error']
        self.assertIn('Configure', errors[0]['text'])
        self.assertEqual(errors[1]['id'], 'r1')
        self.assertEqual(errors[2]['id'], 'r2')
        self.assertFalse(any(e['event'] == 'agent_started' for e in results))

    def test_failed_turn_pauses_queue(self):
        # A local server holds each request briefly, then closes: every turn ends in a provider error,
        # and the second prompt is reliably queued before the first one fails.
        server = socket.socket(); server.bind(('127.0.0.1', 0)); server.listen()
        self.addCleanup(server.close)
        def serve():
            while True:
                try:
                    conn, _ = server.accept()
                except OSError:
                    return
                time.sleep(0.5); conn.close()
        threading.Thread(target=serve, daemon=True).start()
        base = f'http://127.0.0.1:{server.getsockname()[1]}/v1'
        script = ''.join(json.dumps(m) + '\n' for m in [
            {'type': 'configure', 'base_url': base, 'model': 'm', 'api_key': '', 'workspace': str(ROOT)},
            {'type': 'ask', 'id': 'a', 'text': 'one', 'when': 'queue'},
            {'type': 'ask', 'id': 'b', 'text': 'two', 'when': 'queue'}])
        proc = subprocess.Popen([sys.executable, '-S', str(ROOT / 'backend/worker.py')], stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, text=True, cwd=ROOT)
        proc.stdin.write(script); proc.stdin.flush()
        events = []
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            events.append(json.loads(proc.stdout.readline()))
            if events[-1]['event'] == 'queue_changed' and events[-1]['paused'] and events[-1]['running'] is None:
                break
        proc.stdin.write(json.dumps({'type': 'queue_clear'}) + '\n' + json.dumps({'type': 'shutdown'}) + '\n')
        proc.stdin.close()
        events += [json.loads(line) for line in proc.stdout]
        proc.wait(timeout=5)
        queued = {e['request_id']: e['id'] for e in events if e['event'] == 'queued'}
        finished = [e for e in events if e['event'] == 'agent_finished']
        self.assertEqual([(e['id'], e['outcome']) for e in finished], [(queued['a'], 'error')])
        self.assertEqual(events[-1]['items'], [])
        self.assertFalse(any(e['event'] == 'agent_started' and e['id'] == queued['b'] for e in events))


if __name__ == '__main__':
    unittest.main()


class SlowToolThenAnswer:
    """First call asks for a slow tool; later calls record what they saw and answer."""
    def __init__(self, command='sleep 1'):
        self.command, self.seen, self.calls = command, [], 0

    def complete(self, messages, tools, emit, cancel):
        self.calls += 1
        self.seen.append(json.loads(json.dumps(messages)))
        if self.calls == 1:
            return {'role': 'assistant', 'content': '', 'tool_calls': [
                {'id': 's1', 'type': 'function', 'function': {'name': 'run_command',
                                                              'arguments': json.dumps({'command': self.command})}}]}
        return {'role': 'assistant', 'content': 'answer %d' % self.calls}

    def cancel(self): pass


class SteerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.rec = Recorder()
        self.sup = TurnSupervisor(self.rec)

    def tearDown(self):
        self.sup.shutdown(timeout=3)
        self.temp.cleanup()

    def use(self, provider):
        self.agent = Agent(CONFIG, self.temp.name, self.sup.agent_emit, provider=provider)
        self.sup.set_agent(self.agent)
        return provider

    def test_steer_joins_running_turn_after_tool_results(self):
        p = self.use(SlowToolThenAnswer())
        first = self.sup.submit('tool please', 'now')
        self.rec.wait(lambda e: e['event'] == 'tool_started')
        steer = self.sup.submit('also check README', 'steer', request_id='r1')
        self.assertEqual(self.rec.wait(lambda e: e['event'] == 'queued' and e['id'] == steer)['when'], 'steer')
        self.rec.wait(lambda e: e['event'] == 'steer_delivered' and steer in e['ids'])
        self.assertEqual(self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] == first)['outcome'], 'done')
        second_call = p.seen[1]
        roles = [m['role'] for m in second_call]
        # The steering prompt comes after the tool result, never between the tool call and its result.
        self.assertEqual(roles[-2:], ['tool', 'user'])
        # G3: steers are framed with their ledger id and keep the verbatim text.
        self.assertTrue(second_call[-1]['content'].startswith('[Sent by the user while you were working (R2,'))
        self.assertTrue(second_call[-1]['content'].endswith('also check README'))
        self.assertFalse(self.rec.of('steer_returned'))
        self.assertEqual(p.calls, 2)

    def test_steer_returned_and_requeued_when_turn_ends_without_tool_call(self):
        p = self.use(GatedProvider())
        first = self.sup.submit('first', 'now')
        self.rec.wait(lambda e: e.get('text') == 'working on first')
        steer = self.sup.submit('late thought', 'steer')
        changed = self.rec.wait(lambda e: e['event'] == 'queue_changed' and e.get('steering'))
        self.assertEqual(changed['steering'][0]['id'], steer)
        p.release.release()
        returned = self.rec.wait(lambda e: e['event'] == 'steer_returned')
        self.assertEqual((returned['id'], returned['requeued']), (steer, True))
        self.rec.wait(lambda e: e.get('text') == 'working on late thought')
        p.release.release()
        self.assertEqual(self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] == steer)['outcome'], 'done')
        self.assertEqual(p.prompts, ['first', 'late thought'])

    def test_steer_without_requeue_is_only_reported(self):
        p = self.use(GatedProvider())
        self.sup.submit('first', 'now')
        self.rec.wait(lambda e: e.get('text') == 'working on first')
        steer = self.sup.submit('gui keeps order', 'steer', request_id='g1', requeue=False)
        p.release.release()
        returned = self.rec.wait(lambda e: e['event'] == 'steer_returned')
        self.assertEqual((returned['id'], returned['request_id'], returned['requeued'], returned['prompt']),
                         (steer, 'g1', False, 'gui keeps order'))
        time.sleep(0.2)
        self.assertEqual(p.prompts, ['first'])

    def test_steer_when_idle_queues_normally(self):
        p = self.use(GatedProvider())
        item = self.sup.submit('idle steer', 'steer')
        self.assertEqual(self.rec.wait(lambda e: e['event'] == 'queued' and e['id'] == item)['when'], 'queue')
        self.rec.wait(lambda e: e.get('text') == 'working on idle steer')
        p.release.release()

    def test_unsteer_interrupts_the_turn_and_runs_the_prompt_itself(self):
        p = self.use(GatedProvider())
        first = self.sup.submit('first', 'now')
        self.rec.wait(lambda e: e.get('text') == 'working on first')
        steer = self.sup.submit('actually stop and do this', 'steer', request_id='s1', requeue=False)
        self.rec.wait(lambda e: e['event'] == 'queue_changed' and e.get('steering'))
        self.assertTrue(self.sup.unsteer('s1', 'i1'))
        escalated = self.rec.wait(lambda e: e['event'] == 'steer_escalated')
        self.assertEqual((escalated['request_id'], escalated['escalated'], escalated['new_request_id']),
                         ('s1', True, 'i1'))
        # The interrupt stops the running turn and carries the steer's ledger entry, not a new one.
        self.assertEqual(self.rec.wait(lambda e: e['event'] == 'interrupting')['id'], first)
        item = self.rec.wait(lambda e: e['event'] == 'queued' and e['request_id'] == 'i1')
        self.assertEqual(item['when'], 'interrupt')
        self.assertEqual(item['ledger_id'], escalated['ledger_id'])
        self.assertEqual(self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] == first)['outcome'],
                         'cancelled')
        self.rec.wait(lambda e: e.get('text') == 'working on actually stop and do this')
        p.release.release()
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] == item['id'])
        self.assertEqual(p.prompts, ['first', 'actually stop and do this'])
        self.assertFalse(self.rec.of('steer_returned'))
        # The queue is not paused by a cancel it asked for itself.
        self.assertFalse(self.rec.of('queue_changed')[-1]['paused'])
        self.assertNotEqual(steer, item['id'])

    def test_unsteer_does_nothing_once_the_steer_was_delivered(self):
        p = self.use(SlowToolThenAnswer())
        self.sup.submit('tool please', 'now')
        self.rec.wait(lambda e: e['event'] == 'tool_started')
        self.sup.submit('also check README', 'steer', request_id='s1')
        self.rec.wait(lambda e: e['event'] == 'steer_delivered')
        self.assertFalse(self.sup.unsteer('s1', 'i1'))
        escalated = self.rec.wait(lambda e: e['event'] == 'steer_escalated')
        self.assertEqual((escalated['escalated'], escalated['ledger_id']), (False, None))
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertFalse(self.rec.of('interrupting'))
        self.assertEqual(p.calls, 2)

    def test_remove_withdraws_a_steer_the_turn_has_not_taken(self):
        p = self.use(GatedProvider())
        first = self.sup.submit('first', 'now')
        self.rec.wait(lambda e: e.get('text') == 'working on first')
        steer = self.sup.submit('never mind this', 'steer', request_id='s1', requeue=False)
        self.rec.wait(lambda e: e['event'] == 'queue_changed' and e.get('steering'))
        self.sup.remove(steer)
        removed = self.rec.wait(lambda e: e['event'] == 'steer_removed')
        self.assertEqual((removed['id'], removed['request_id']), (steer, 's1'))
        self.assertEqual(self.rec.of('queue_changed')[-1]['steering'], [])
        p.release.release()
        self.assertEqual(self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] == first)['outcome'], 'done')
        time.sleep(0.2)
        # The turn never saw it, and it does not come back as a queue item either.
        self.assertEqual(p.prompts, ['first'])
        self.assertFalse(self.rec.of('steer_delivered'))
        self.assertFalse(self.rec.of('steer_returned'))
        if self.agent.track_requests and removed['ledger_id']:
            self.assertEqual(self.agent.requests.find(removed['ledger_id'])['status'], 'cancelled_by_user')

    def test_remove_refuses_a_steer_already_delivered(self):
        self.use(SlowToolThenAnswer())
        self.sup.submit('tool please', 'now')
        self.rec.wait(lambda e: e['event'] == 'tool_started')
        steer = self.sup.submit('also check README', 'steer', request_id='s1')
        self.rec.wait(lambda e: e['event'] == 'steer_delivered')
        with self.assertRaises(ValueError):
            self.sup.remove(steer)
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertFalse(self.rec.of('steer_removed'))

    def test_queue_steer_upgrades_a_queued_prompt(self):
        p = self.use(SlowToolThenAnswer('sleep 1'))
        self.sup.submit('tool please', 'now')
        self.rec.wait(lambda e: e['event'] == 'tool_started')
        queued = self.sup.submit('upgrade me', 'queue')
        self.sup.steer(queued)
        self.rec.wait(lambda e: e['event'] == 'steer_delivered' and queued in e['ids'])
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertTrue(p.seen[1][-1]['content'].endswith('\nupgrade me'))
        with self.assertRaises(ValueError):
            self.sup.steer('nope')
