"""Rotating diagnostics log (issue SQAM): location, permissions, rotation and what must never be in it.

Nothing here touches the real data directory (XDG_DATA_HOME is redirected), the keyring or the network.
"""
import json
import logging
import os
import stat
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest import mock
from pathlib import Path

from relay_core import logs
from relay_core.agent import Agent
from relay_core.provider import ProviderConfig

ROOT = Path(__file__).resolve().parents[1]
SECRET_KEY = 'sk-relaytestkey0123456789abcdef'
PROMPT = 'delete the staging database and tell nobody'


class LogFileTests(unittest.TestCase):
    def test_usage_state_is_private_timestamped_and_excludes_secrets(self):
        logs.configure("worker", level="info")
        event = {"event": "usage_limits", "preset": "guest:claude:work",
                 "account": "work", "source": "subscription_poll",
                 "windows": [{"kind": "5h", "used_percent": 31.2,
                              "resets_at": 1790320000}],
                 "resets_available": 1, "resets_expire_at": 1792703299,
                 "access_token": "SECRET"}
        with mock.patch.dict(os.environ, {"RELAY_LOG_ORIGIN": "test"}):
            logs.usage_state(event)
        path = self.dir / "usage-states.jsonl"
        row = json.loads(path.read_text())
        self.assertEqual(row["preset"], "guest:claude:work")
        self.assertEqual(row["windows"][0]["used_percent"], 31.2)
        self.assertEqual(row["resets_expire_at"], 1792703299)
        self.assertIn("ts", row)
        self.assertNotIn("SECRET", path.read_text())
        self.assertEqual(path.stat().st_mode & 0o777, 0o600)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.home = os.environ.get('XDG_DATA_HOME')
        os.environ['XDG_DATA_HOME'] = self.temp.name
        self.dir = Path(self.temp.name) / 'relay' / 'logs'

    def tearDown(self):
        logger = logging.getLogger('relay')
        for handler in list(logger.handlers):
            logger.removeHandler(handler); handler.close()
        logger.addHandler(logging.NullHandler())
        if self.home is None:
            os.environ.pop('XDG_DATA_HOME', None)
        else:
            os.environ['XDG_DATA_HOME'] = self.home
        self.temp.cleanup()

    def text(self, name='worker.log'):
        return (self.dir / name).read_text(encoding='utf-8')

    def test_routing_draws_from_a_test_go_only_to_a_private_data_directory(self):
        """A test writes draws under its own XDG_DATA_HOME, never into the real profile's file."""
        with mock.patch.dict(os.environ, {'RELAY_LOG_LEVEL': 'info', 'RELAY_LOG_ORIGIN': 'test'}):
            logs.routing_draw({'surface': 'probe'})
        self.assertIn('"surface":"probe"', self.text(logs.ROUTING_DRAWS))
        fake_home = Path(self.temp.name) / 'home'
        with mock.patch.dict(os.environ, {'XDG_DATA_HOME': str(fake_home / '.local/share'),
                                          'RELAY_LOG_ORIGIN': 'test', 'RELAY_LOG_LEVEL': 'info'}), \
                mock.patch.object(Path, 'home', return_value=fake_home):
            logs.routing_draw({'surface': 'leak'})
            os.environ.pop('XDG_DATA_HOME')
            logs.routing_draw({'surface': 'leak'})
        self.assertFalse((fake_home / '.local/share/relay/logs' / logs.ROUTING_DRAWS).exists())

    def test_a_fatal_signal_leaves_a_python_traceback(self):
        """A worker that dies of SIGSEGV writes its stacks to worker-faults.log.

        Nothing else sees one: the signal never reaches the logging module, and the GUI discards
        the worker's stderr deliberately. Run in a child, because this one really does crash.
        """
        child = """
import ctypes, sys
sys.path.insert(0, {backend!r})
from relay_core import logs
logs.configure('worker', level='info')
ctypes.CDLL(None).prctl(4, 0)     # PR_SET_DUMPABLE=0: no core, no crash report to the machine
ctypes.string_at(0)               # and now a real segmentation fault
""".format(backend=str(ROOT / 'backend'))
        environment = dict(os.environ, XDG_DATA_HOME=self.temp.name)
        done = subprocess.run([sys.executable, '-c', child], env=environment,
                              capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, -11, done.stderr)   # still dies of the signal
        report = (self.dir / 'worker-faults.log').read_text(encoding='utf-8')
        self.assertIn('Fatal Python error', report)
        self.assertIn('Segmentation fault', report)
        self.assertIn('Current thread', report)               # with the Python stack that was on it
        self.assertEqual(stat.S_IMODE((self.dir / 'worker-faults.log').stat().st_mode), 0o600)

    def test_fault_reports_are_off_when_logging_is_off(self):
        logs.configure('worker', level='off')
        self.assertFalse((self.dir / 'worker-faults.log').exists())

    def test_location_permissions_and_format(self):
        log = logs.configure('worker', pane='pane-1', level='info')
        logs.event(log, 'turn_start', session='s1', turn='t1', model='glm-5.3', host='api.z.ai')
        for handler in log.handlers:
            handler.flush()
        path = self.dir / 'worker.log'
        self.assertTrue(path.exists())
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(self.dir.stat().st_mode), 0o700)
        line = self.text().strip().splitlines()[-1]
        self.assertIn('pane=pane-1', line)
        self.assertIn('turn_start', line)
        self.assertIn('session=s1', line)
        self.assertIn('model=glm-5.3', line)
        self.assertIn('host=api.z.ai', line)
        self.assertRegex(line, r'^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z INFO relay ')

    def test_keys_are_masked_even_when_something_logs_one(self):
        log = logs.configure('worker', level='debug')
        logs.event(log, 'oops', note=SECRET_KEY, auth='Bearer ' + SECRET_KEY)
        log.error('api_key=%s', SECRET_KEY)
        for handler in log.handlers:
            handler.flush()
        body = self.text()
        self.assertNotIn(SECRET_KEY, body)
        self.assertNotIn('relaytestkey', body)
        self.assertIn('redacted', body)

    def test_prompt_text_only_at_the_opt_in_verbose_level(self):
        log = logs.configure('worker', level='info')
        self.assertFalse(logs.verbose())
        logs.event(log, 'turn_start', turn='t1')       # so the file exists either way
        logs.prompt(log, 'turn_prompt', PROMPT, turn='t1')
        for handler in log.handlers:
            handler.flush()
        self.assertNotIn(PROMPT, self.text())
        log = logs.configure('worker', level='verbose')
        self.assertTrue(logs.verbose())
        logs.prompt(log, 'turn_prompt', PROMPT, turn='t1')
        for handler in log.handlers:
            handler.flush()
        self.assertIn('delete the staging database', self.text())

    def test_rotation_keeps_three_backups(self):
        original = logs.MAX_BYTES
        logs.MAX_BYTES = 2048
        try:
            log = logs.configure('worker', level='info')
            for index in range(400):
                logs.event(log, 'tool', turn='t1', call=f'c{index}', tool='run_command', ms=index)
            for handler in log.handlers:
                handler.flush()
        finally:
            logs.MAX_BYTES = original
        names = sorted(p.name for p in self.dir.glob('worker.log*') if not p.name.endswith('.lock'))
        self.assertEqual(names, ['worker.log', 'worker.log.1', 'worker.log.2', 'worker.log.3'])
        for name in names:
            self.assertEqual(stat.S_IMODE((self.dir / name).stat().st_mode), 0o600)
            self.assertLessEqual((self.dir / name).stat().st_size, 2048 + 4096)

    def test_off_writes_nothing(self):
        logs.configure('worker', level='off')
        logs.event(logs.get('agent'), 'turn_start', session='s1')
        self.assertFalse((self.dir / 'worker.log').exists())

    def test_a_turn_logs_its_shape_but_never_its_content(self):
        log = logs.configure('worker', pane='pane-9', level='info')

        class Provider:
            def complete(self, messages, tools, emit, cancel):
                emit({'event': 'delta', 'text': 'the model answer'})
                return {'role': 'assistant', 'content': 'the model answer'}
            def cancel(self): pass

        with tempfile.TemporaryDirectory() as workspace:
            agent = Agent(ProviderConfig('http://127.0.0.1:12345/v1', 'mock', ''), workspace,
                          lambda event: None, provider=Provider())
            agent.ask(PROMPT)
        for handler in log.handlers:
            handler.flush()
        body = self.text()
        self.assertIn('turn_start', body)
        self.assertIn('turn_end', body)
        self.assertIn('outcome=done', body)
        self.assertNotIn(PROMPT, body)                 # no prompt at the default level
        self.assertNotIn('the model answer', body)     # and no answer, ever

    def failing_turn(self, error):
        class Provider:
            def complete(self, messages, tools, emit, cancel):
                raise error
            def cancel(self): pass

        events = []
        with tempfile.TemporaryDirectory() as workspace:
            agent = Agent(ProviderConfig('http://127.0.0.1:12345/v1', 'mock', ''), workspace,
                          events.append, provider=Provider())
            agent.ask(PROMPT)
        return [e['text'] for e in events if e.get('event') == 'error']

    def test_an_unexpected_turn_exception_keeps_its_traceback_in_the_log(self):
        # Card #D09N: the pane is told the class only, the private log gets the traceback.
        log = logs.configure('worker', pane='pane-9', level='info')
        errors = self.failing_turn(AttributeError("module 'x' has no attribute 'config_preset'"))
        for handler in log.handlers:
            handler.flush()
        self.assertEqual(errors, ['Agent error (AttributeError).'])
        body = self.text()
        self.assertIn('turn_exception', body)
        self.assertIn('Traceback', body)
        self.assertIn('config_preset', body)
        self.assertNotIn(PROMPT, body)

    def test_a_missing_attribute_after_a_source_edit_says_to_restart(self):
        logs.configure('worker', pane='pane-9', level='info')
        with mock.patch.object(logs, 'source_changed', return_value=True):
            errors = self.failing_turn(AttributeError('gone'))
            other = self.failing_turn(KeyError('k'))
        self.assertEqual(len(errors), 1)
        self.assertIn('Restart the agent', errors[0])
        self.assertEqual(other, ['Agent error (KeyError).'])

    def test_source_changed_compares_the_tree_with_the_one_loaded(self):
        logs._build_id.cache_clear()
        self.addCleanup(logs._build_id.cache_clear)
        with mock.patch.dict(os.environ, {'RELAY_BUILD_ID': ''}), \
                mock.patch.object(logs, '_source_fingerprint', return_value='source-a'):
            self.assertEqual(logs._build_id(), 'source-a')
            self.assertFalse(logs.source_changed())
        with mock.patch.dict(os.environ, {'RELAY_BUILD_ID': ''}), \
                mock.patch.object(logs, '_source_fingerprint', return_value='source-b'):
            self.assertTrue(logs.source_changed())
        with mock.patch.dict(os.environ, {'RELAY_BUILD_ID': 'pkg-1'}), \
                mock.patch.object(logs, '_source_fingerprint', return_value='source-c'):
            self.assertFalse(logs.source_changed())   # a packaged build is not a checkout


class WorkerLogTests(unittest.TestCase):
    """The worker writes the file itself, with the pane id the GUI passes in the environment."""

    def test_worker_start_is_logged_with_the_pane_id(self):
        with tempfile.TemporaryDirectory() as home:
            environment = {**os.environ, 'XDG_DATA_HOME': home, 'RELAY_PANE_ID': 'pane-abc',
                           'RELAY_LOG_LEVEL': 'info', 'RELAY_KEYRING': 'off'}
            payload = json.dumps({'type': 'shutdown'}) + '\n'
            proc = subprocess.run([sys.executable, '-S', str(ROOT / 'backend/worker.py')], input=payload,
                                  text=True, capture_output=True, timeout=20, cwd=ROOT, env=environment)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            body = (Path(home) / 'relay' / 'logs' / 'worker.log').read_text(encoding='utf-8')
            self.assertIn('pane=pane-abc', body)
            self.assertIn('worker_start', body)
            self.assertIn('worker_stop', body)


class ConcurrentWriterTests(unittest.TestCase):
    """Several panes share worker.log; rotation under a lock must not lose or interleave lines."""

    def test_threads_rotating_together_keep_whole_lines(self):
        with tempfile.TemporaryDirectory() as home:
            previous = os.environ.get('XDG_DATA_HOME')
            os.environ['XDG_DATA_HOME'] = home
            original = logs.MAX_BYTES
            logs.MAX_BYTES = 4096
            try:
                log = logs.configure('worker', level='info')
                def write(index):
                    for step in range(60):
                        logs.event(log, 'tool', turn=f't{index}', call=f'c{step}', tool='run_command')
                threads = [threading.Thread(target=write, args=(i,)) for i in range(4)]
                for thread in threads: thread.start()
                for thread in threads: thread.join()
                for handler in log.handlers:
                    handler.flush()
            finally:
                logs.MAX_BYTES = original
                logger = logging.getLogger('relay')
                for handler in list(logger.handlers):
                    logger.removeHandler(handler); handler.close()
                logger.addHandler(logging.NullHandler())
                if previous is None:
                    os.environ.pop('XDG_DATA_HOME', None)
                else:
                    os.environ['XDG_DATA_HOME'] = previous
            written = 0
            for path in (Path(home) / 'relay' / 'logs').glob('worker.log*'):
                if path.name.endswith('.lock'):
                    continue
                for line in path.read_text(encoding='utf-8').splitlines():
                    self.assertIn('tool turn=t', line)   # never half a line from another writer
                    written += 1
            self.assertGreaterEqual(written, 60)         # older records may have rotated away
