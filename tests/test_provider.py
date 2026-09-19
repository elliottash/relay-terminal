import email.message
import email.utils
import io
import json
import os
import socket
import threading
import time
import unittest
import urllib.error
from unittest import mock
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from relay_core.provider import (CONNECT_TIMEOUT, MAX_OUTPUT_TOKENS, ChatProvider, ProviderConfig,
                                 ProviderError, ProviderStalled, ProviderTruncated, Cancelled,
                                 validate_stall_timeout)


def event(delta=None, finish=None):
    return ('data: ' + json.dumps({'choices': [{'delta': delta or {}, 'finish_reason': finish}]}) + '\n\n').encode()

class StreamTests(unittest.TestCase):
    def setUp(self):
        self.provider = ChatProvider(ProviderConfig('http://127.0.0.1:1234/v1', 'mock', ''))
        self.events = []
        self.cancel = threading.Event()

    def parse(self, data):
        return self.provider._stream(io.BytesIO(data), self.events.append, self.cancel)

    def test_text_and_unicode(self):
        result = self.parse(event({'content':'Hello 世界'}) + event(finish='stop') + b'data: [DONE]\n\n')
        self.assertEqual(result['content'], 'Hello 世界')

    def test_fragmented_tool_arguments_and_reasoning(self):
        data = event({'reasoning_content':'some provider reasoning'})
        data += event({'tool_calls':[{'index':0,'id':'call1','type':'function','function':{'name':'run_command','arguments':'{"comm'}}]})
        data += event({'tool_calls':[{'index':0,'function':{'arguments':'and":"pwd"}'}}]})
        data += event(finish='tool_calls') + b'data: [DONE]\n\n'
        result = self.parse(data)
        self.assertEqual(result['reasoning_content'], 'some provider reasoning')
        self.assertEqual(json.loads(result['tool_calls'][0]['function']['arguments']), {'command':'pwd'})
        # Reasoning is never answer text; it streams as thinking_delta (protocol 11).
        self.assertFalse(any(e.get('text') == 'some provider reasoning' for e in self.events if e['event'] == 'delta'))
        self.assertIn({'event': 'thinking_delta', 'text': 'some provider reasoning'}, self.events)
        done = [e for e in self.events if e['event'] == 'thinking_done']
        self.assertEqual(len(done), 1)
        self.assertEqual(done[0]['chars'], len('some provider reasoning'))

    def test_multiple_tool_calls(self):
        data = event({'tool_calls':[
            {'index':1,'id':'b','function':{'name':'read_file','arguments':'{"path":"b"}'}},
            {'index':0,'id':'a','function':{'name':'read_file','arguments':'{"path":"a"}'}}]})
        result = self.parse(data + event(finish='tool_calls') + b'data: [DONE]\n\n')
        self.assertEqual([c['id'] for c in result['tool_calls']], ['a', 'b'])

    def test_truncated_stream_rejected(self):
        with self.assertRaises(ProviderError): self.parse(event({'content':'unfinished'}))
        with self.assertRaises(ProviderTruncated): self.parse(event(finish='length') + b'data: [DONE]\n\n')

    # --- a step cut off at the output limit (owner report, 2026-09-18) -------------------------
    def usage_event(self, usage):
        return ('data: ' + json.dumps({'choices': [], 'usage': usage}) + '\n\n').encode()

    def test_a_cut_off_step_says_what_it_spent_and_keeps_no_partial_tool_call(self):
        # Pinned at Relay's ceiling on an endpoint it cannot name, so there is nothing left to raise.
        self.provider = ChatProvider(ProviderConfig('http://127.0.0.1:1234/v1', 'mock', '',
                                                    max_tokens=MAX_OUTPUT_TOKENS))
        data = self.usage_event({'prompt_tokens': 48000, 'completion_tokens': MAX_OUTPUT_TOKENS})
        data += event({'reasoning_content': 'thinking that fills the budget'})
        data += event({'tool_calls': [{'index': 0, 'id': 'c1', 'type': 'function',
                                       'function': {'name': 'run_command', 'arguments': '{"comm'}}]})
        data += event(finish='length') + b'data: [DONE]\n\n'
        with self.assertRaises(ProviderTruncated) as caught:
            self.parse(data)
        exc = caught.exception
        self.assertEqual(exc.reason, 'length')
        self.assertTrue(exc.produced)                 # a tool-call fragment reached the user
        self.assertIsNone(exc.partial)                # cut-off arguments are never kept
        self.assertIn(str(MAX_OUTPUT_TOKENS), str(exc))
        self.assertIn("as much as this model gives", str(exc))
        # The tokens it spent are reported before the failure, not dropped with it.
        self.assertEqual([e for e in self.events if e['event'] == 'usage'][-1]['usage']['completion_tokens'],
                         MAX_OUTPUT_TOKENS)

    def test_a_cut_off_answer_keeps_the_text_the_user_already_saw(self):
        data = event({'reasoning_content': 'some thinking'}) + event({'content': 'Half an ans'})
        with self.assertRaises(ProviderTruncated) as caught:
            self.parse(data + event(finish='length') + b'data: [DONE]\n\n')
        self.assertEqual(caught.exception.partial['content'], 'Half an ans')
        self.assertEqual(caught.exception.partial['role'], 'assistant')
        self.assertEqual(caught.exception.partial['reasoning_content'], 'some thinking')

    def test_reasoning_alone_is_not_produced_and_a_filtered_response_reads_differently(self):
        with self.assertRaises(ProviderTruncated) as caught:
            self.parse(event({'reasoning_content': 'only thinking'}) + event(finish='length') + b'data: [DONE]\n\n')
        self.assertFalse(caught.exception.produced)   # nothing was on the user's screen: retryable
        self.assertIsNone(caught.exception.partial)
        with self.assertRaises(ProviderTruncated) as caught:
            self.parse(event(finish='content_filter') + b'data: [DONE]\n\n')
        self.assertEqual(caught.exception.reason, 'content_filter')
        self.assertIn('filtered this response', str(caught.exception))

    def test_a_model_at_its_own_documented_cap_is_not_told_to_raise_the_limit(self):
        """Gemini stops at 65,536 with Relay's ceiling at 131,072: raising it would change nothing."""
        from relay_core.presets import PRESETS
        gemini = PRESETS['gemini']
        provider = ChatProvider(ProviderConfig(gemini.base_url, gemini.model, 'k'))
        self.assertEqual(provider.config.max_tokens, 65_536)      # automatic: the model's own cap
        with self.assertRaises(ProviderTruncated) as caught:
            provider._stream(io.BytesIO(event(finish='length') + b'data: [DONE]\n\n'),
                             self.events.append, self.cancel)
        self.assertEqual(caught.exception.model_cap, 65_536)
        self.assertIn('65536-token output budget', str(caught.exception))
        self.assertIn('as much as this model gives', str(caught.exception))
        self.assertNotIn('Raise the output token limit', str(caught.exception))

    def test_a_lower_output_limit_is_told_to_raise_it(self):
        provider = ChatProvider(ProviderConfig('http://127.0.0.1:1234/v1', 'mock', '', max_tokens=8192))
        with self.assertRaises(ProviderTruncated) as caught:
            provider._stream(io.BytesIO(event(finish='length') + b'data: [DONE]\n\n'),
                             self.events.append, self.cancel)
        self.assertIn('Raise the output token limit', str(caught.exception))

    def test_malformed_duplicate_and_error(self):
        with self.assertRaises(ProviderError): self.parse(b'data: {"error":{}}\n\n')
        with self.assertRaises(ProviderError): self.parse(event({'tool_calls':[{'index':100}]}))
        with self.assertRaises(ProviderError):
            self.provider._normalize({'tool_calls':[{'id':'a','type':'function','function':{'name':'f','arguments':'{}'}}, {'id':'a','type':'function','function':{'name':'f','arguments':'{}'}}]})

    def test_cancelled(self):
        self.cancel.set()
        with self.assertRaises(Cancelled): self.parse(event({'content':'no'}))

    def test_configuration_guards(self):
        for url in ['http://example.com/v1', 'https://user:pass@example.com/v1', 'file:///etc/passwd', 'https://example.com/v1?key=x']:
            with self.assertRaises(ValueError): ProviderConfig(url, 'm', 'k').validate()
        with self.assertRaises(ValueError): ProviderConfig('https://example.com/v1','m','').validate()
        with self.assertRaises(ValueError): ProviderConfig('https://example.com/v1','m','k',{'messages':[]}).validate()
        ProviderConfig('https://api.z.ai/api/paas/v4','glm-5.3','k',{'thinking':{'type':'enabled'},'reasoning_effort':'high'}).validate()

    def test_an_unnamed_endpoint_keeps_the_conservative_default(self):
        # An endpoint Relay cannot name may cap output far below what its window suggests, so
        # automatic stays at 32768 there — and a number the user typed for their own server stands.
        from relay_core.session_protocol import provider_config
        self.assertEqual(ProviderConfig('https://example.com/v1', 'm', 'k').max_tokens, 32768)
        request = {'base_url': 'https://example.com/v1', 'model': 'm', 'api_key': 'k'}
        self.assertEqual(provider_config(request).max_tokens, 32768)
        self.assertEqual(provider_config({**request, 'max_tokens': 4096}).max_tokens, 4096)
        self.assertEqual(provider_config({**request, 'max_tokens': MAX_OUTPUT_TOKENS}).max_tokens,
                         MAX_OUTPUT_TOKENS)

    def test_automatic_asks_each_model_for_what_it_documents(self):
        """Card #Z79Y: the default is the model's own cap, not one number for every provider."""
        from relay_core.presets import PRESETS
        from relay_core.session_protocol import provider_config
        for preset_id, expected in (('glm-coding', 131_072), ('gemini', 65_536),
                                    ('openai', 128_000), ('openrouter', 32_768)):
            preset = PRESETS[preset_id]
            with self.subTest(preset_id):
                self.assertEqual(ProviderConfig(preset.base_url, preset.model, 'k').max_tokens, expected)
                # Pinning Relay's ceiling never sends a model more than it takes: Gemini would
                # refuse the request outright rather than answer at its own limit.
                self.assertEqual(ProviderConfig(preset.base_url, preset.model, 'k',
                                                max_tokens=MAX_OUTPUT_TOKENS).max_tokens, expected)
                # A smaller number is the user's own choice and is left alone.
                self.assertEqual(ProviderConfig(preset.base_url, preset.model, 'k',
                                                max_tokens=4096).max_tokens, 4096)
        self.assertEqual(provider_config({'preset': 'gemini', 'api_key': 'k'}).max_tokens, 65_536)

    def test_a_local_server_still_gets_a_quarter_of_its_window(self):
        config = ProviderConfig('http://127.0.0.1:8080/v1', 'bonsai-2-27b', '', local=True,
                                context_window=131_072)
        self.assertEqual(config.max_tokens, 32_768)              # automatic, clamped to the quarter
        self.assertEqual(ProviderConfig('http://127.0.0.1:8080/v1', 'm', '', local=True,
                                        context_window=32_768).max_tokens, 8_192)

class HTTPTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.requests = []
        outer = cls
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args): pass
            def do_POST(self):
                body = self.rfile.read(int(self.headers['Content-Length']))
                outer.requests.append({'path':self.path,'body':json.loads(body),'authorization':self.headers.get('Authorization')})
                if self.path.startswith('/redirect'):
                    self.send_response(307); self.send_header('Location','http://127.0.0.1:9/steal'); self.end_headers(); return
                if self.path.startswith('/error'):
                    self.send_response(401); self.end_headers(); self.wfile.write(b'SECRET_ECHO'); return
                if self.path.startswith('/json'):
                    response = json.dumps({'choices':[{'finish_reason':'stop','message':{'role':'assistant','content':'JSON_OK'}}]}).encode()
                    self.send_response(200); self.send_header('Content-Type','application/json'); self.send_header('Content-Length',str(len(response))); self.end_headers(); self.wfile.write(response); return
                self.send_response(200); self.send_header('Content-Type','text/event-stream'); self.end_headers()
                self.wfile.write(event({'content':'HTTP_OK'}) + event(finish='stop') + b'data: [DONE]\n\n')
        cls.server = ThreadingHTTPServer(('127.0.0.1',0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True); cls.thread.start()
        cls.base = f'http://127.0.0.1:{cls.server.server_port}'
    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown(); cls.server.server_close(); cls.thread.join()

    def complete(self, path):
        provider = ChatProvider(ProviderConfig(self.base + path, 'test-model', 'TEST_SECRET', {'thinking':{'type':'enabled'}}))
        return provider.complete([{'role':'user','content':'hello'}], [], lambda x:None, threading.Event())

    def test_http_transport(self):
        result = self.complete('/v1')
        self.assertEqual(result['content'], 'HTTP_OK')
        request = self.requests[-1]
        self.assertEqual(request['path'], '/v1/chat/completions')
        self.assertEqual(request['authorization'], 'Bearer TEST_SECRET')
        self.assertTrue(request['body']['stream'])
        self.assertEqual(request['body']['thinking']['type'], 'enabled')

    def test_attribute_error_after_cancel_is_a_stop(self):
        # cancel() closes the response from another thread; http.client then raises AttributeError.
        from unittest import mock
        provider = ChatProvider(ProviderConfig(self.base + '/v1', 'test-model', 'TEST_SECRET'))
        messages = [{'role':'user','content':'hello'}]
        cancel = threading.Event()
        def closed_by_cancel(*args):
            cancel.set()
            raise AttributeError("'NoneType' object has no attribute 'readline'")
        with mock.patch.object(provider, '_stream', side_effect=closed_by_cancel):
            with self.assertRaises(Cancelled):
                provider.complete(messages, [], lambda x:None, cancel)
        with mock.patch.object(provider, '_stream', side_effect=AttributeError('real bug')):
            with self.assertRaises(AttributeError):
                provider.complete(messages, [], lambda x:None, threading.Event())

    def test_json_fallback(self):
        self.assertEqual(self.complete('/json')['content'], 'JSON_OK')

    def test_redirect_not_followed(self):
        with self.assertRaises(ProviderError): self.complete('/redirect')

    def test_http_errors_do_not_echo_provider_body(self):
        with self.assertRaises(ProviderError) as ctx: self.complete('/error')
        self.assertNotIn('SECRET_ECHO', str(ctx.exception))
        self.assertIn('401', str(ctx.exception))


class StallTests(unittest.TestCase):
    """Issue SQAM: the idle deadline must cover every streamed chunk, and no socket may outlive a turn.

    The server only ever answers on loopback; no test here reaches the network or the keyring.
    """
    STALL = 1.0            # the smallest allowed deadline, so the suite stays quick

    @classmethod
    def setUpClass(cls):
        cls.release = threading.Event()
        outer = cls
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args): pass
            def do_POST(self):
                self.rfile.read(int(self.headers.get('Content-Length') or 0))
                self.send_response(200); self.send_header('Content-Type', 'text/event-stream'); self.end_headers()
                self.wfile.flush()
                try:
                    if self.path.startswith('/keepalive'):
                        # Headers, then only SSE comments: bytes keep arriving but no answer does.
                        while not outer.release.wait(0.05):
                            self.wfile.write(b': ping\n\n'); self.wfile.flush()
                        return
                    if self.path.startswith('/partial'):
                        self.wfile.write(event({'content': 'half an ans'})); self.wfile.flush()
                    elif self.path.startswith('/truncated'):
                        self.wfile.write(event({'content': 'no finish reason'})); self.wfile.flush()
                        return                              # closes: an ordinary ProviderError
                    elif self.path.startswith('/ok'):
                        self.wfile.write(event({'content': 'HTTP_OK'}) + event(finish='stop') + b'data: [DONE]\n\n')
                        self.wfile.flush(); return
                    outer.release.wait(30)                  # headers sent, then nothing at all
                except OSError:
                    return
        cls.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True); cls.thread.start()
        cls.base = f'http://127.0.0.1:{cls.server.server_port}'

    @classmethod
    def tearDownClass(cls):
        cls.release.set()
        cls.server.shutdown(); cls.server.server_close(); cls.thread.join()

    def provider(self, path):
        return ChatProvider(ProviderConfig(self.base + path, 'test-model', ''), stall_timeout=self.STALL)

    def complete(self, provider, cancel=None):
        return provider.complete([{'role': 'user', 'content': 'hello'}], [], lambda x: None,
                                 cancel or threading.Event())

    def test_headers_then_nothing_ends_the_turn(self):
        provider = self.provider('/silent')
        started = time.monotonic()
        with self.assertRaises(ProviderStalled) as ctx:
            self.complete(provider)
        self.assertLess(time.monotonic() - started, self.STALL + 5)
        self.assertIn('sent nothing for 1 s', str(ctx.exception))
        self.assertFalse(ctx.exception.produced)      # nothing arrived, so the turn may be retried
        self.assertFalse(provider.response_open())

    def test_keepalives_do_not_hold_a_dead_turn_open(self):
        # The observed 12-minute "thinking" pane: the socket timeout never fires while comments drip in.
        provider = self.provider('/keepalive')
        with self.assertRaises(ProviderStalled):
            self.complete(provider)
        self.assertFalse(provider.response_open())

    def test_a_started_answer_is_not_retryable(self):
        provider = self.provider('/partial')
        with self.assertRaises(ProviderStalled) as ctx:
            self.complete(provider)
        self.assertTrue(ctx.exception.produced)
        self.assertFalse(provider.response_open())

    def test_no_response_is_left_open_after_any_end_state(self):
        # done
        provider = self.provider('/ok')
        self.assertEqual(self.complete(provider)['content'], 'HTTP_OK')
        self.assertFalse(provider.response_open())
        # failed (the stream ends without a finish reason)
        provider = self.provider('/truncated')
        with self.assertRaises(ProviderError):
            self.complete(provider)
        self.assertFalse(provider.response_open())
        # cancelled, while a read is blocked in the stalled stream
        provider = self.provider('/silent')
        cancel = threading.Event()
        outcome = {}

        def run():
            try:
                self.complete(provider, cancel)
            except BaseException as exc:                     # noqa: BLE001 - recorded, then asserted
                outcome['error'] = exc
        worker = threading.Thread(target=run); worker.start()
        for _ in range(200):
            if provider.response_open():
                break
            time.sleep(0.01)
        cancel.set(); provider.cancel()
        worker.join(timeout=10)
        self.assertFalse(worker.is_alive())
        self.assertIsInstance(outcome.get('error'), Cancelled)
        self.assertFalse(provider.response_open())
        # stalled
        provider = self.provider('/silent')
        with self.assertRaises(ProviderStalled):
            self.complete(provider)
        self.assertFalse(provider.response_open())

    def test_stall_timeout_bounds(self):
        self.assertEqual(validate_stall_timeout(90), 90.0)
        for bad in (0.5, 3600, 'soon', True, None):
            with self.assertRaises(ValueError):
                validate_stall_timeout(bad)

    def test_environment_override_wins_over_the_agent_option(self):
        # RELAY_PROVIDER_TIMEOUT (the 2026-09-17 stopgap) now feeds the one idle deadline.
        provider = self.provider('/silent')
        self.assertEqual(provider.stall_timeout, self.STALL)
        with mock.patch.dict(os.environ, {'RELAY_PROVIDER_TIMEOUT': '300'}):
            self.assertEqual(provider.stall_timeout, 300.0)
            self.assertEqual(provider.open_timeout, 300.0)   # headers get the same room
        for bad in ('', 'soon', '0'):                        # unset/invalid falls back, tiny is clamped
            with mock.patch.dict(os.environ, {'RELAY_PROVIDER_TIMEOUT': bad}):
                self.assertIn(provider.stall_timeout, (self.STALL, 5.0))
        # The header budget never drops below the connect floor.
        self.assertEqual(provider.open_timeout, CONNECT_TIMEOUT)

    def test_no_response_headers_at_all_is_a_retryable_stall(self):
        # A provider that withholds the 200 until its first token is ready: the owner's
        # "Provider connection failed (TimeoutError)". It must read as a stall, not a bad base URL,
        # so the turn is retried instead of failing outright.
        listener = socket.socket()
        listener.bind(('127.0.0.1', 0)); listener.listen(1)
        held = []

        def accept():
            try:
                held.append(listener.accept()[0])     # accepted, then never answered
            except OSError:
                pass
        thread = threading.Thread(target=accept, daemon=True); thread.start()
        provider = ChatProvider(ProviderConfig(f'http://127.0.0.1:{listener.getsockname()[1]}/v1',
                                               'test-model', ''), stall_timeout=1.0)
        try:
            with mock.patch('relay_core.provider.CONNECT_TIMEOUT', 1.0), \
                 self.assertRaises(ProviderStalled) as ctx:
                self.complete(provider)
            self.assertEqual(ctx.exception.stage, 'connect')
            self.assertIn('did not answer within', str(ctx.exception))
            self.assertFalse(ctx.exception.produced)   # retryable: nothing was sent or done
            self.assertFalse(provider.response_open())
        finally:
            listener.close()
            for connection in held:
                connection.close()
            thread.join(timeout=5)


class RetryTests(unittest.TestCase):
    """A provider's transient refusal — 429, a 5xx — is asked again, the way Claude Code asks.

    Every wait here is either a ``Retry-After: 0`` or a backoff shrunk on the instance, so the
    suite stays quick. The server only ever answers on loopback.
    """

    @classmethod
    def setUpClass(cls):
        cls.counts = {}
        outer = cls

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args): pass

            def refuse(self, status, retry_after=None, body=b''):
                self.send_response(status)
                if retry_after is not None:
                    self.send_header('Retry-After', retry_after)
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def ok(self):
                self.send_response(200); self.send_header('Content-Type', 'text/event-stream')
                self.end_headers()
                self.wfile.write(event({'content': 'RETRY_OK'}) + event(finish='stop')
                                 + b'data: [DONE]\n\n')

            def do_POST(self):
                self.rfile.read(int(self.headers.get('Content-Length') or 0))
                mount = self.path.rsplit('/chat', 1)[0]
                outer.counts[mount] = outer.counts.get(mount, 0) + 1
                seen = outer.counts[mount]
                if mount == '/rate/v1' and seen == 1:
                    return self.refuse(429, '0', b'{"error": "rate limit hit"}')
                if mount == '/after/v1' and seen == 1:
                    return self.refuse(429, '1')
                if mount == '/always/v1':
                    return self.refuse(429, '0')
                if mount == '/slow/v1':
                    return self.refuse(429, '2')
                if mount == '/server/v1' and seen == 1:
                    return self.refuse(500)
                if mount == '/auth/v1':
                    return self.refuse(401, body=b'SECRET_ECHO')
                if mount == '/overflow/v1':
                    return self.refuse(500, body=b'{"error": "prompt exceeds the available context size"}')
                if mount == '/loading/v1' and seen == 1:
                    return self.refuse(503)
                self.ok()

        cls.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True); cls.thread.start()
        cls.base = f'http://127.0.0.1:{cls.server.server_port}'

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown(); cls.server.server_close(); cls.thread.join()

    def provider(self, mount, **config):
        return ChatProvider(ProviderConfig(self.base + mount, 'test-model', '', **config))

    def complete(self, provider, cancel=None):
        events = []
        result = provider.complete([{'role': 'user', 'content': 'hello'}], [], events.append,
                                   cancel or threading.Event())
        return result, events

    def seen(self, mount) -> int:
        return self.counts.get(mount, 0)

    def test_a_rate_limited_request_is_sent_again(self):
        provider = self.provider('/rate/v1')
        result, events = self.complete(provider)
        self.assertEqual(result['content'], 'RETRY_OK')
        self.assertEqual(self.seen('/rate/v1'), 2)
        retry = next(e for e in events if e['event'] == 'provider_retry')
        self.assertEqual(retry['reason'], 'http')
        self.assertEqual(retry['attempt'], 1)
        self.assertEqual(retry['max_attempts'], 6)
        self.assertIn('429', retry['text'])
        self.assertTrue(any('429' in e.get('text', '') for e in events if e['event'] == 'status'))
        self.assertFalse(provider.response_open())

    def test_the_wait_a_retry_after_header_names_is_honoured(self):
        provider = self.provider('/after/v1')
        started = time.monotonic()
        result, _ = self.complete(provider)
        self.assertEqual(result['content'], 'RETRY_OK')
        self.assertGreaterEqual(time.monotonic() - started, 1.0)
        self.assertEqual(self.seen('/after/v1'), 2)

    def test_a_refusal_that_never_lifts_fails_after_the_retries(self):
        provider = self.provider('/always/v1')
        events = []
        with self.assertRaises(ProviderError) as caught:
            provider.complete([{'role': 'user', 'content': 'hello'}], [], events.append,
                              threading.Event())
        self.assertIn('429', str(caught.exception))
        self.assertEqual(self.seen('/always/v1'), 7)          # the first try and six retries
        notes = [e for e in events if e['event'] == 'provider_retry']
        self.assertEqual([e['attempt'] for e in notes], [1, 2, 3, 4, 5, 6])
        self.assertFalse(provider.response_open())

    def test_a_server_error_is_sent_again_too(self):
        provider = self.provider('/server/v1')
        provider.HTTP_RETRY_BASE_S = provider.HTTP_RETRY_CEILING_S = 0.01   # no header: backoff
        result, _ = self.complete(provider)
        self.assertEqual(result['content'], 'RETRY_OK')
        self.assertEqual(self.seen('/server/v1'), 2)

    def test_a_refusal_about_the_request_itself_is_not_retried(self):
        provider = self.provider('/auth/v1')
        with self.assertRaises(ProviderError) as caught:
            self.complete(provider)
        self.assertIn('401', str(caught.exception))
        self.assertNotIn('SECRET_ECHO', str(caught.exception))
        self.assertEqual(self.seen('/auth/v1'), 1)

    def test_a_local_server_is_not_generically_retried(self):
        # Its 5xx are deterministic — the prompt does not fit — so the sentence arrives at once.
        provider = self.provider('/overflow/v1', local=True)
        with self.assertRaises(ProviderError) as caught:
            self.complete(provider)
        self.assertIn('no longer fits', str(caught.exception))
        self.assertEqual(self.seen('/overflow/v1'), 1)

    def test_a_local_server_still_loading_is_waited_out(self):
        provider = self.provider('/loading/v1', local=True)
        provider.LOADING_RETRY_S = 0.01
        result, events = self.complete(provider)
        self.assertEqual(result['content'], 'RETRY_OK')
        self.assertEqual(self.seen('/loading/v1'), 2)
        self.assertIn({'event': 'status', 'text': 'The local server is loading its model…'}, events)
        self.assertFalse(any(e['event'] == 'provider_retry' for e in events))

    def test_stop_during_the_wait_ends_the_turn(self):
        provider = self.provider('/slow/v1')
        cancel = threading.Event()
        threading.Timer(0.2, cancel.set).start()
        with self.assertRaises(Cancelled):
            self.complete(provider, cancel)
        self.assertFalse(provider.response_open())

    def test_retry_after_header_parsing(self):
        provider = self.provider('/rate/v1')

        def refusal(headers: dict):
            message = email.message.Message()
            for name, value in headers.items():
                message[name] = value
            return urllib.error.HTTPError('http://x/', 429, 'Too Many Requests', message, None)
        self.assertEqual(provider._retry_after_s(refusal({'Retry-After': '2.5'})), 2.5)
        self.assertEqual(provider._retry_after_s(refusal({'retry-after-ms': '250'})), 0.25)
        self.assertEqual(provider._retry_after_s(refusal({'Retry-After': '3600'})), 60.0)
        past = email.utils.formatdate(time.time() - 30, usegmt=True)
        self.assertEqual(provider._retry_after_s(refusal({'Retry-After': past})), 0.0)
        soon = email.utils.formatdate(time.time() + 3, usegmt=True)
        waited = provider._retry_after_s(refusal({'Retry-After': soon}))
        self.assertGreaterEqual(waited, 1.0)
        self.assertLessEqual(waited, 3.0)
        self.assertIsNone(provider._retry_after_s(refusal({'Retry-After': 'soon'})))
        self.assertIsNone(provider._retry_after_s(refusal({})))


class OpenRouterReasoningTests(unittest.TestCase):
    def test_reasoning_field_is_preserved_and_not_displayed(self):
        provider = ChatProvider(ProviderConfig('http://127.0.0.1:1234/v1', 'mock', '', {'reasoning': {'effort': 'high'}}))
        events = []
        data = b': OPENROUTER PROCESSING\n\n' + event({'reasoning': 'thinking hard'})
        data += event({'content': 'Done.'}) + event(finish='stop') + b'data: [DONE]\n\n'
        result = provider._stream(io.BytesIO(data), events.append, threading.Event())
        self.assertEqual(result['reasoning'], 'thinking hard')
        self.assertEqual(result['content'], 'Done.')
        self.assertFalse(any(e.get('text') == 'thinking hard' for e in events if e['event'] == 'delta'))
        self.assertIn({'event': 'thinking_delta', 'text': 'thinking hard'}, events)
        self.assertTrue(any(e.get('event') == 'status' for e in events))
