import json
import shlex
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
        first = self.sup.submit('first', 'queue')
        # The dispatcher thread takes the head as soon as it is there, and a queued prompt reports
        # the position it was put at. Reading the three positions without waiting raced that
        # thread: on a loaded machine `first` was already dequeued when `second` was submitted, so
        # the positions came out [0, 0, 1]. Wait for the head to start, and what the other two
        # report is the queue's own ordering rather than a race.
        self.rec.wait(lambda e: e['event'] == 'agent_started' and e['id'] == first)
        ids = [first, self.sup.submit('second', 'queue'), self.sup.submit('third', 'queue')]
        self.assertEqual([e['position'] for e in self.rec.of('queued')], [0, 0, 1])
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
        # Cancellation must race a provider already entered, not the earlier start event.
        self.rec.wait(lambda e: e['event'] == 'delta')
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
        # Cancellation must race a provider already entered, not the earlier start event.
        self.rec.wait(lambda e: e['event'] == 'delta')
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
        # A local server that accepts a request and then holds the connection open until this test
        # closes it: the turn fails exactly when the test says so, not after a fixed wait.
        #
        # It used to close after 0.5 s, on the reasoning that the second prompt would be queued
        # inside that window. Under load it was not: the first turn failed with an empty queue, so
        # nothing paused (which is right — "the pause resets when the queue empties"), the second
        # prompt then ran, and the `queue_changed {paused}` this loop waits for never came. The loop
        # checked its deadline only between reads, so it sat in a blocking `readline()` on a worker
        # that was waiting on stdin: the test never returned and took the whole `backend-and-bash`
        # target to its 600 s ceiling with it. Both halves of that are fixed here — the failure is
        # ordered against the second prompt, and nothing in this test can block forever.
        server = socket.socket(); server.bind(('127.0.0.1', 0)); server.listen()
        self.addCleanup(server.close)
        fail_now = threading.Event()
        self.addCleanup(fail_now.set)          # never leave the serve thread parked on it
        def serve():
            while True:
                try:
                    conn, _ = server.accept()
                except OSError:
                    return
                fail_now.wait(30)
                conn.close()
        threading.Thread(target=serve, daemon=True).start()
        base = f'http://127.0.0.1:{server.getsockname()[1]}/v1'
        script = ''.join(json.dumps(m) + '\n' for m in [
            {'type': 'configure', 'base_url': base, 'model': 'm', 'api_key': '', 'workspace': str(ROOT)},
            {'type': 'ask', 'id': 'a', 'text': 'one', 'when': 'queue'},
            {'type': 'ask', 'id': 'b', 'text': 'two', 'when': 'queue'}])
        proc = subprocess.Popen([sys.executable, '-S', str(ROOT / 'backend/worker.py')], stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, text=True, cwd=ROOT)
        self.addCleanup(proc.kill)
        # A readline() on a worker that is waiting on stdin never returns on its own. Killing the
        # worker closes the pipe, readline() returns '' and the loop below ends: a broken run fails
        # this one test instead of hanging the suite.
        watchdog = threading.Timer(60, proc.kill)
        watchdog.start()
        self.addCleanup(watchdog.cancel)
        proc.stdin.write(script); proc.stdin.flush()
        events = []
        while True:
            line = proc.stdout.readline()
            self.assertTrue(line, 'the worker stopped before the queue paused: ' + json.dumps(events)[-2000:])
            events.append(json.loads(line))
            if events[-1]['event'] == 'queued' and events[-1].get('request_id') == 'b':
                fail_now.set()          # both prompts are in the queue: let the first turn fail
            if events[-1]['event'] == 'queue_changed' and events[-1]['paused'] and events[-1]['running'] is None:
                break
        proc.stdin.write(json.dumps({'type': 'queue_clear'}) + '\n' + json.dumps({'type': 'shutdown'}) + '\n')
        proc.stdin.close()
        events += [json.loads(line) for line in proc.stdout]
        proc.wait(timeout=30)
        queued = {e['request_id']: e['id'] for e in events if e['event'] == 'queued'}
        finished = [e for e in events if e['event'] == 'agent_finished']
        self.assertEqual([(e['id'], e['outcome']) for e in finished], [(queued['a'], 'error')])
        # The last `queue_changed`, not the last event of all: the worker may say something else on
        # its way out (KeyError: 'items' under load), and what this is about is the clear.
        self.assertEqual([e for e in events if e['event'] == 'queue_changed'][-1]['items'], [])
        self.assertFalse(any(e['event'] == 'agent_started' and e['id'] == queued['b'] for e in events))


if __name__ == '__main__':
    unittest.main()


class SlowToolThenAnswer:
    """First call asks for a tool that runs until `release()`; later calls record and answer.

    The tool used to be `sleep 1`, which every steering test then raced: the steering prompt has to
    reach the supervisor *before* the turn's next step boundary, or the turn never takes it and
    reports `steer_returned` instead of `steer_delivered`. On a loaded machine the second went by
    before the test thread woke from `tool_started` and submitted, and the test waited five seconds
    for an event that was never coming. The command now waits for a file, so the tool cannot end
    until the test creates it — no window to miss.
    """
    def __init__(self, gate: Path):
        self.gate = Path(gate)
        self.command = 'until [ -e %s ]; do sleep 0.02; done' % shlex.quote(str(self.gate))
        self.seen, self.calls = [], 0

    def release(self):
        """Let the tool finish, and with it the turn's next step boundary."""
        self.gate.write_bytes(b'')

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

    def slow_tool(self):
        """A provider whose first tool call waits in the workspace until `release()`."""
        return self.use(SlowToolThenAnswer(Path(self.temp.name) / 'let-the-tool-finish'))

    def test_steer_joins_running_turn_after_tool_results(self):
        p = self.slow_tool()
        first = self.sup.submit('tool please', 'now')
        self.rec.wait(lambda e: e['event'] == 'tool_started')
        steer = self.sup.submit('also check README', 'steer', request_id='r1')
        self.assertEqual(self.rec.wait(lambda e: e['event'] == 'queued' and e['id'] == steer)['when'], 'steer')
        p.release()          # the steer is in: the tool may reach its step boundary now
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
        p = self.slow_tool()
        self.sup.submit('tool please', 'now')
        self.rec.wait(lambda e: e['event'] == 'tool_started')
        self.sup.submit('also check README', 'steer', request_id='s1')
        p.release()
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
        p = self.slow_tool()
        self.sup.submit('tool please', 'now')
        self.rec.wait(lambda e: e['event'] == 'tool_started')
        steer = self.sup.submit('also check README', 'steer', request_id='s1')
        p.release()
        self.rec.wait(lambda e: e['event'] == 'steer_delivered')
        with self.assertRaises(ValueError):
            self.sup.remove(steer)
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertFalse(self.rec.of('steer_removed'))

    def test_queue_steer_upgrades_a_queued_prompt(self):
        p = self.slow_tool()
        self.sup.submit('tool please', 'now')
        self.rec.wait(lambda e: e['event'] == 'tool_started')
        queued = self.sup.submit('upgrade me', 'queue')
        self.sup.steer(queued)
        p.release()
        self.rec.wait(lambda e: e['event'] == 'steer_delivered' and queued in e['ids'])
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertTrue(p.seen[1][-1]['content'].endswith('\nupgrade me'))
        with self.assertRaises(ValueError):
            self.sup.steer('nope')


class ConsoleFieldTests(unittest.TestCase):
    """`surface`, `screen`, `readonly` and `queue_move` — protocol 33, card #AGNT.

    The helper's own FIFO had reorder and no steering; the pane's queue had steering and no
    reorder; neither could say which of four consoles had asked. There is one queue now, and
    these are the four things it gained on the way.
    """

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

    def test_every_event_of_a_turn_carries_the_surface_that_asked(self):
        p = self.use(GatedProvider())
        item = self.sup.submit('what is in Options?', 'now', surface='options')
        started = self.rec.wait(lambda e: e['event'] == 'agent_started')
        self.assertEqual(started['surface'], 'options')
        delta = self.rec.wait(lambda e: e['event'] == 'delta')
        self.assertEqual(delta['surface'], 'options')     # through `agent_emit`, not only here
        p.release.release()
        finished = self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] == item)
        self.assertEqual(finished['surface'], 'options')
        self.assertEqual(self.rec.of('queued')[0]['surface'], 'options')

    def test_a_pane_sends_no_surface_and_no_event_grows_the_field(self):
        p = self.use(GatedProvider())
        item = self.sup.submit('ls', 'now')
        self.rec.wait(lambda e: e['event'] == 'agent_started')
        p.release.release()
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] == item)
        self.assertFalse([e for e in self.rec.events if 'surface' in e])

    def test_the_queue_rows_say_which_console_is_waiting(self):
        p = self.use(GatedProvider())
        self.sup.submit('first', 'queue', surface='switchboard')
        self.rec.wait(lambda e: e['event'] == 'agent_started')
        self.sup.submit('second', 'queue', surface='sessions')
        rows = self.rec.of('queue_changed')[-1]['items']
        self.assertEqual([r.get('surface') for r in rows], ['sessions'])
        p.release.release(); p.release.release()

    def test_the_screen_hint_reaches_the_model_and_not_the_record(self):
        p = self.use(GatedProvider())
        item = self.sup.submit('which row is that?', 'now', surface='options',
                               screen='  Appearance ›  Copy on select  ')
        self.rec.wait(lambda e: e['event'] == 'agent_started')
        p.release.release()
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] == item)
        # The model is told what is on screen…
        self.assertTrue(p.prompts[0].startswith('On screen now: Appearance › Copy on select\n\n'))
        self.assertTrue(p.prompts[0].endswith('which row is that?'))
        # …and what the queue and the pane hold is the person's own words.
        self.sup.submit('and this one?', 'queue', surface='options', screen='another row')
        row = self.rec.of('queue_changed')[-1]['items'][-1]
        self.assertEqual(row['preview'], 'and this one?')
        self.assertNotIn('On screen', json.dumps(self.rec.of('queue_changed')))
        p.release.release()
        # …and so is everything `ask` makes out of the prompt. The hint used to be composed into
        # the string before `ask` saw it, so the **title** was "On screen now: Appearance › Copy
        # on select which row is…" — which is what the Switchboard console's pane header showed,
        # and what the Sessions list would have kept for ever.
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] != item)
        self.assertNotIn('On screen', self.agent.title)
        self.assertTrue(self.agent.title)
        # The turn's own record is the words as typed: the checkpoint a rewind goes back to and
        # the ledger row the request audit shows. (The *message* still carries the hint in its
        # prefix, beside plan-mode notes and attachments — that is what a note is.)
        self.assertNotIn('On screen', json.dumps([c.get('prompt') for c in self.agent.checkpoints.items]))
        self.assertEqual([c.get('prompt') for c in self.agent.checkpoints.items][-1], 'and this one?')
        self.assertNotIn('On screen', json.dumps(self.agent.requests.items))
        self.assertIn('On screen now: Appearance', p.prompts[0])

    def test_a_readonly_turn_is_read_only_for_exactly_that_turn(self):
        seen = []
        p = self.use(GatedProvider())
        original = self.agent.set_readonly

        def record(on, original=original):
            seen.append(on)
            original(on)
        self.agent.set_readonly = record
        item = self.sup.submit('survey the project', 'now', readonly=True)
        self.rec.wait(lambda e: e['event'] == 'agent_started')
        p.release.release()
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] == item)
        self.assertEqual(seen, [True, False])
        self.assertFalse(self.agent.readonly_turn)

    def test_a_queued_card_turn_carries_its_mode_and_card(self):
        """`ask {mode, card}` rides the queue item (card #CTRN): a card turn is an ordinary turn.

        Before this card a second prompt on a busy card was refused with `board_busy`, because
        the card's turn was a runner of its own with no queue. The rows are what the §12 strip
        draws, so the mode and the card have to be on them and not only inside the worker.
        """
        p = self.use(GatedProvider())
        first = self.sup.submit('what is this card about?', 'now',
                                surface='card:CTRN', mode='discuss', card='CTRN')
        started = self.rec.wait(lambda e: e['event'] == 'agent_started' and e['id'] == first)
        self.assertEqual((started['mode'], started['card_id']), ('discuss', 'CTRN'))
        self.sup.submit('now plan it', 'queue', surface='card:CTRN', mode='plan', card='CTRN')
        row = self.rec.of('queue_changed')[-1]['items'][-1]
        self.assertEqual((row['mode'], row['card_id'], row['surface']),
                         ('plan', 'CTRN', 'card:CTRN'))
        self.assertEqual(self.rec.of('queued')[-1]['mode'], 'plan')
        p.release.release()
        finished = self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] == first)
        self.assertEqual((finished['mode'], finished['card_id']), ('discuss', 'CTRN'))
        p.release.release()
        # A pane's turn says neither, exactly as it says no surface.
        self.sup.submit('ls', 'queue')
        self.assertNotIn('card_id', self.rec.of('queued')[-1])
        p.release.release()

    def test_the_card_scope_is_opened_for_the_turn_and_closed_after_it(self):
        """`set_card_turn` brackets the ask, and the bracket closes when the ask raises.

        It is `set_readonly`'s shape (above), on the two lines beside it, because a card turn is
        the same kind of thing: a constraint on one turn, never a narrower tool list (#CTRN).
        """
        seen = []
        p = self.use(GatedProvider())
        self.agent.set_card_turn = lambda mode, card, seen=seen: seen.append((mode, card))
        item = self.sup.submit('plan this', 'now', mode='plan', card='CTRN')
        self.rec.wait(lambda e: e['event'] == 'agent_started')
        p.release.release()
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] == item)
        self.assertEqual(seen, [('plan', 'CTRN'), (None, None)])

        class Boom:
            def complete(self, messages, tools, emit, cancel):
                raise RuntimeError('the provider fell over')

            def cancel(self):
                pass

        seen.clear()
        self.sup.set_agent(Agent(CONFIG, self.temp.name, self.sup.agent_emit, provider=Boom()))
        self.sup.agent.set_card_turn = lambda mode, card, seen=seen: seen.append((mode, card))
        item = self.sup.submit('discuss this', 'now', mode='discuss', card='CTRN')
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] == item)
        self.assertEqual(seen, [('discuss', 'CTRN'), (None, None)])
        # A turn that names no card never reaches the agent's card half at all.
        seen.clear()
        item = self.sup.submit('and this', 'now')
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] == item)
        self.assertEqual(seen, [])

    def test_a_card_turn_is_refused_the_writers_at_call_time_and_told_why(self):
        """The executor's half of the stage rule (#CTRN): refused when called, never withheld.

        A board-less agent is the case that decides where this lives: the scope the board holds
        is the same answer, but it is only there when a board is. The sentence is `CardScope`'s
        own, so the model is told what Discuss and Plan are for and that writing code is Execute.
        """
        self.use(GatedProvider())
        self.agent.set_card_turn('plan', 'CTRN')
        self.assertEqual(self.agent.card_turn, ('plan', 'CTRN'))
        for name in ('write_file', 'edit_file', 'run_command'):
            with self.assertRaises(ValueError) as caught:
                self.agent._prepare(name, {'path': 'x'})
            self.assertIn('Execute', str(caught.exception))
            self.assertIn('#CTRN', str(caught.exception))
        # What a card turn reads is not refused, and neither is anything once the turn is over.
        Path(self.temp.name, 'card.md').write_text('# a card\n')
        self.agent._prepare('read_file', {'path': 'card.md'})
        self.agent.set_card_turn(None, None)
        self.assertIsNone(self.agent.card_turn)
        self.agent._prepare('write_file', {'path': 'x', 'content': 'y'})

    def test_a_queue_row_shows_what_was_typed_when_the_prompt_is_not_it(self):
        """`submit {preview}` (card #CTRN): a card turn's prompt is not the owner's question.

        `board_ask` builds the model's prompt out of the card's seed block and the mode's brief,
        so the row on the card page — and the request ledger's — would read "[Switchboard card
        #CRD1 …] You are Relay's Switchboard agent…" where the question belongs. It is `screen`'s
        rule one field along: the record is what the person typed.
        """
        p = self.use(GatedProvider())
        self.sup.submit('running', 'now')
        self.rec.wait(lambda e: e['event'] == 'agent_started')
        self.sup.submit('[Switchboard card #CRD1] the brief\n\nand the queue?', 'queue',
                        surface='card:CRD1', mode='discuss', card='CRD1',
                        preview='and the queue?')
        row = self.rec.of('queue_changed')[-1]['items'][-1]
        self.assertEqual(row['preview'], 'and the queue?')
        self.assertEqual(self.agent.requests.items[-1]['text'], 'and the queue?')
        p.release.release()
        # …and the model still gets the whole prompt.
        self.rec.wait(lambda e: e['event'] == 'agent_started' and e.get('card_id') == 'CRD1')
        p.release.release()
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and e.get('card_id') == 'CRD1')
        self.assertIn('[Switchboard card #CRD1]', p.prompts[-1])

    def test_a_mode_without_a_card_is_refused_before_anything_is_queued(self):
        self.use(GatedProvider())
        for mode, card in (('plan', ''), ('', 'CTRN'), ('pl\nan', 'CTRN'), ('plan', 'C' * 65)):
            with self.assertRaises(ValueError):
                self.sup.submit('go', 'queue', mode=mode, card=card)
        self.assertEqual(self.rec.of('queued'), [])

    def test_queue_move_reorders_a_waiting_prompt(self):
        p = self.use(GatedProvider())
        self.sup.submit('running', 'queue')
        self.rec.wait(lambda e: e['event'] == 'agent_started')
        second = self.sup.submit('second', 'queue')
        third = self.sup.submit('third', 'queue')
        self.sup.move(third, 0, request_id='m1')
        rows = self.rec.of('queue_changed')[-1]['items']
        self.assertEqual([r['id'] for r in rows], [third, second])
        ack = self.rec.of('queue_ack')[-1]
        self.assertEqual((ack['id'], ack['op'], ack['item'], ack['to']), ('m1', 'move', third, 0))
        for _ in range(3):
            p.release.release()

    def test_queue_move_clamps_rather_than_refusing_and_refuses_what_is_not_queued(self):
        p = self.use(GatedProvider())
        self.sup.submit('running', 'queue')
        self.rec.wait(lambda e: e['event'] == 'agent_started')
        second = self.sup.submit('second', 'queue')
        self.sup.move(second, 99)                       # a drag past the end means "last"
        self.assertEqual([r['id'] for r in self.rec.of('queue_changed')[-1]['items']], [second])
        with self.assertRaises(ValueError):
            self.sup.move('nope', 0)
        with self.assertRaises(ValueError):
            self.sup.move(second, 'first')
        p.release.release(); p.release.release()

    def test_remove_and_clear_answer_with_the_request_id_when_one_is_given(self):
        p = self.use(GatedProvider())
        self.sup.submit('running', 'queue')
        self.rec.wait(lambda e: e['event'] == 'agent_started')
        second = self.sup.submit('second', 'queue')
        self.sup.remove(second, 'r7')
        self.assertEqual(self.rec.of('queue_ack')[-1], {'event': 'queue_ack', 'id': 'r7',
                                                        'op': 'remove', 'item': second})
        self.sup.submit('third', 'queue')
        self.sup.clear('r8')
        self.assertEqual(self.rec.of('queue_ack')[-1], {'event': 'queue_ack', 'id': 'r8',
                                                        'op': 'clear'})
        # Nothing new on the wire for a caller that sends no id, as every GUI before #AGNT does.
        self.sup.submit('fourth', 'queue')
        before = len(self.rec.of('queue_ack'))
        self.sup.clear()
        self.assertEqual(len(self.rec.of('queue_ack')), before)
        p.release.release()
