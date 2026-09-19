# SPDX-License-Identifier: AGPL-3.0-or-later
"""The five smoke checks (`relay-local.py smoke`), against scripted servers.

Each server here is one of the ways a local endpoint really fails: a conforming one, one whose
template dies on the *second* tool call of a conversation, and one that writes the call into the
answer as text. The shapes are the ones measured against llama-server on 2026-09-18, as in
tests/test_provider_local.py.
"""
import json
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from relay_core import localsmoke

DONE = b'data: [DONE]\n\n'


def chunk(delta=None, finish=None, **top):
    return b'data: ' + json.dumps({'choices': [{'index': 0, 'delta': delta or {}, 'finish_reason': finish}],
                                   **top}).encode() + b'\n\n'


def usage_chunk(messages):
    chars = len(json.dumps(messages, ensure_ascii=False))
    return b'data: ' + json.dumps({'choices': [], 'usage': {'prompt_tokens': max(1, chars // 4),
                                                            'completion_tokens': 20}}).encode() + b'\n\n'


def answer(messages, text):
    return b''.join([chunk({'content': text}), chunk(finish='stop'), usage_chunk(messages), DONE])


def call(messages, key):
    arguments = json.dumps({'key': key})
    return b''.join([
        chunk({'tool_calls': [{'index': 0, 'id': 'call_' + key, 'type': 'function',
                               'function': {'name': 'lookup_number', 'arguments': ''}}]}),
        chunk({'tool_calls': [{'index': 0, 'function': {'arguments': arguments}}]}),
        chunk(finish='tool_calls'), usage_chunk(messages), DONE])


TAG_PROSE = ('A model writes <tool_call> around a call it could not make natively, and </think> '
             'closes a block of reasoning that was meant for nobody. Both are template artefacts.')


def conforming(body):
    """A server that does everything right."""
    messages = body.get('messages', [])
    tools = body.get('tools')
    if not tools:
        return 200, answer(messages, 'ready now')
    last_user = next((m.get('content', '') for m in reversed(messages) if m.get('role') == 'user'), '')
    if last_user.startswith('In plain prose'):
        return 200, answer(messages, TAG_PROSE)
    results = [m for m in messages if m.get('role') == 'tool']
    if not results:
        return 200, call(messages, 'alpha')
    if len(results) == 1:
        return 200, call(messages, 'beta')
    return 200, answer(messages, 'The two numbers add up to 12.')


def breaks_on_the_second_call(body):
    """A template that handles one tool call and 400s on the next: the failure a one-call test misses."""
    messages = body.get('messages', [])
    if any(m.get('role') == 'tool' for m in messages):
        return 400, json.dumps({'error': {'code': 400, 'message': 'Failed to parse tool call arguments'}}).encode()
    return conforming(body)


def writes_the_call_as_text(body):
    """No --jinja, or a template with no tool section: the call arrives in `content`."""
    messages = body.get('messages', [])
    tools = body.get('tools')
    if not tools:
        return 200, answer(messages, 'ready now')
    last_user = next((m.get('content', '') for m in reversed(messages) if m.get('role') == 'user'), '')
    if last_user.startswith('In plain prose'):
        return 200, answer(messages, TAG_PROSE)
    return 200, answer(messages, '<tool_call>\n{"name": "lookup_number", "arguments": {"key": "alpha"}}\n</tool_call>')


def calls_a_tool_on_the_tag_question(body):
    """A parser that fires on the literal string a user can type."""
    messages = body.get('messages', [])
    last_user = next((m.get('content', '') for m in reversed(messages) if m.get('role') == 'user'), '')
    if last_user.startswith('In plain prose'):
        return 200, call(messages, 'think')
    return conforming(body)


class Fake:
    """An OpenAI-compatible server whose answers come from ``policy(body)``."""

    def __init__(self, policy=conforming, model='fake-model'):
        self.policy, self.model, self.requests = policy, model, []
        outer = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                if self.path.rstrip('/').endswith('/v1/models'):
                    payload = json.dumps({'object': 'list', 'data': [{'id': outer.model}]}).encode()
                    self.send_response(200); self.send_header('Content-Type', 'application/json')
                    self.send_header('Content-Length', str(len(payload))); self.end_headers()
                    self.wfile.write(payload)
                    return
                self.send_response(404); self.send_header('Content-Length', '0'); self.end_headers()

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                outer.requests.append(body)
                status, payload = outer.policy(body)
                kind = 'text/event-stream' if status == 200 else 'application/json'
                self.send_response(status); self.send_header('Content-Type', kind)
                if status != 200:
                    self.send_header('Content-Length', str(len(payload)))
                self.end_headers()
                try:
                    self.wfile.write(payload); self.wfile.flush()
                except OSError:
                    pass

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f'http://127.0.0.1:{self.server.server_port}/v1'

    def close(self):
        self.server.shutdown(); self.server.server_close(); self.thread.join()


def results(result):
    return {check['name']: check for check in result['checks']}


class SmokeTests(unittest.TestCase):
    def run_against(self, policy=conforming, model='fake-model', **options):
        server = Fake(policy)
        self.addCleanup(server.close)
        return server, localsmoke.smoke(server.base, model, **options)

    def test_a_conforming_server_passes_all_five(self):
        server, result = self.run_against()
        self.assertTrue(result['ok'], result)
        self.assertEqual([c['name'] for c in result['checks']], list(localsmoke.ORDER))
        self.assertTrue(all(c['ok'] and not c['skipped'] for c in result['checks']), result['checks'])
        self.assertEqual(localsmoke.exit_code(result), 0)
        # Five requests: the plain question, two tool turns, the answer after them, the tag question.
        self.assertEqual(len(server.requests), 5)
        # Relay's own transport, not a second one: usage is asked for and one call at a time.
        self.assertEqual(server.requests[0]['stream_options'], {'include_usage': True})
        self.assertIs(server.requests[1]['parallel_tool_calls'], False)
        self.assertIsNone(server.requests[1].get('api_key'))
        self.assertIn('5/5 checks passed', localsmoke.report(result))

    def test_the_model_id_can_come_from_the_server(self):
        server, result = self.run_against(model='')
        self.assertEqual(result['model'], 'fake-model')
        self.assertTrue(result['ok'], result)
        self.assertEqual(server.requests[0]['model'], 'fake-model')

    def test_a_template_that_dies_on_the_second_call_fails_check_three(self):
        _, result = self.run_against(breaks_on_the_second_call)
        checks = results(result)
        self.assertTrue(checks['answers']['ok'])
        self.assertTrue(checks['tool_call']['ok'])
        self.assertFalse(checks['two_calls']['ok'])
        self.assertFalse(checks['two_calls']['skipped'])
        self.assertIn('400', checks['two_calls']['reason'])
        self.assertFalse(result['ok'])
        self.assertEqual(localsmoke.exit_code(result), 1)

    def test_a_call_written_as_text_fails_check_two_and_says_what_to_do(self):
        _, result = self.run_against(writes_the_call_as_text)
        checks = results(result)
        self.assertFalse(checks['tool_call']['ok'])
        self.assertEqual(checks['tool_call']['reason'],
                         'tool call arrived as text; consider tool_text_recovery or fix the server template')
        self.assertTrue(checks['two_calls']['skipped'])
        # The rest still runs: the report says everything that is wrong, not only the first thing.
        self.assertTrue(checks['tags_in_prose']['ok'], checks['tags_in_prose'])
        self.assertTrue(checks['usage']['ok'], checks['usage'])
        self.assertIn('FAIL', localsmoke.report(result))

    def test_a_parser_that_fires_on_prose_fails_check_four(self):
        _, result = self.run_against(calls_a_tool_on_the_tag_question)
        checks = results(result)
        self.assertTrue(checks['two_calls']['ok'], checks['two_calls'])
        self.assertFalse(checks['tags_in_prose']['ok'])
        self.assertIn('parsed as a tool call', checks['tags_in_prose']['reason'])

    def test_an_implausible_prompt_count_fails_check_five(self):
        def inflated(body):
            status, payload = conforming(body)
            return status, payload.replace(b'"prompt_tokens": ', b'"prompt_tokens": 5000000') \
                if status == 200 else payload

        _, result = self.run_against(inflated)
        checks = results(result)
        self.assertTrue(checks['two_calls']['ok'], checks['two_calls'])
        self.assertFalse(checks['usage']['ok'])
        self.assertIn('not plausible', checks['usage']['reason'])

    def test_no_usage_fails_check_five_only(self):
        def silent(body):
            status, payload = conforming(body)
            return status, b''.join(line + b'\n\n' for line in payload.split(b'\n\n')
                                    if b'"usage"' not in line and line) if status == 200 else payload

        _, result = self.run_against(silent)
        checks = results(result)
        self.assertTrue(checks['tool_call']['ok'] and checks['two_calls']['ok'])
        self.assertFalse(checks['usage']['ok'])
        self.assertIn('no usage', checks['usage']['reason'])

    def test_an_empty_answer_stops_the_run(self):
        def mute(body):
            return 200, answer(body.get('messages', []), '')

        _, result = self.run_against(mute)
        checks = results(result)
        self.assertFalse(checks['answers']['ok'])
        for name in ('tool_call', 'two_calls', 'tags_in_prose'):
            self.assertTrue(checks[name]['skipped'], name)
        self.assertIn('empty content', checks['answers']['reason'])

    def test_a_non_loopback_url_is_refused_without_a_request(self):
        started = time.monotonic()
        result = localsmoke.smoke('https://api.example.com/v1', 'gpt-4')
        self.assertLess(time.monotonic() - started, 1.0)     # nothing was opened
        self.assertFalse(result['ok'])
        self.assertIn('on this machine', result['error'])
        self.assertTrue(all(c['skipped'] for c in result['checks']))
        self.assertEqual(localsmoke.exit_code(result), 2)
        for url in ('http://192.168.0.7:8080/v1', 'http://localhost.evil.example/v1', ''):
            self.assertEqual(localsmoke.exit_code(localsmoke.smoke(url, 'm')), 2, url)

    def test_nothing_serving_is_reported_as_nothing_run(self):
        import socket
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        result = localsmoke.smoke(f'http://127.0.0.1:{port}/v1', 'anything')
        self.assertEqual(localsmoke.exit_code(result), 1)
        self.assertIn('llama-server', results(result)['answers']['reason'])

    def test_a_server_that_lists_no_single_model_asks_for_one(self):
        server = Fake(conforming)
        self.addCleanup(server.close)
        server.model = ''       # /v1/models answers with an unusable row, so nothing can be chosen
        result = localsmoke.smoke(server.base)
        self.assertEqual(localsmoke.exit_code(result), 2)
        self.assertIn('--model', result['error'])


class CommandTests(unittest.TestCase):
    """`scripts/relay-local.py smoke`: the wiring, not the checks."""

    def run_cli(self, *args):
        import os
        import subprocess
        import sys
        from pathlib import Path
        root = Path(__file__).resolve().parents[1]
        return subprocess.run([sys.executable, str(root / 'scripts/relay-local.py'), 'smoke', *args],
                              capture_output=True, text=True, timeout=60,
                              env={**os.environ, 'RELAY_LOCAL_MODELS': '/nonexistent/none.json'})

    def test_a_refused_url_exits_two_and_prints_json_on_request(self):
        done = self.run_cli('https://api.example.com/v1', '--model', 'x', '--json')
        self.assertEqual(done.returncode, 2, done.stderr)
        result = json.loads(done.stdout)
        self.assertFalse(result['ok'])
        self.assertIn('on this machine', result['error'])

    def test_the_report_is_what_it_prints_without_json(self):
        server = Fake(conforming)
        self.addCleanup(server.close)
        done = self.run_cli(server.base, '--model', 'fake-model')
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn('5/5 checks passed', done.stdout)


if __name__ == '__main__':
    unittest.main()
