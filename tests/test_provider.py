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
import urllib.request
from unittest import mock
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from relay_core import provider as provider_module
from relay_core.provider import (CONNECT_TIMEOUT, MAX_OUTPUT_TOKENS, ChatProvider, ProviderConfig,
                                 ProviderError, ProviderStalled, ProviderTruncated, Cancelled, ProviderPreempted,
                                 cache_counts, validate_stall_timeout)


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

    def test_a_leaked_tool_call_is_cut_from_the_stored_answer_but_its_real_calls_survive(self):
        # A gateway that gave up on a tool call mid-arguments streamed the rest as content
        # (card #VN69). The deltas went out as they streamed; what is stored is clean.
        data = event({'content': 'Recording the decisions:\n'})
        data += event({'content': '{"type": "tool_use", "id": "toolu_bdrk_01", "name": "x",\n'})
        data += event({'content': ' "input": {"id": "Y2JW"}}\n'})
        data += event({'tool_calls': [{'index': 0, 'id': 'call1', 'type': 'function',
                                       'function': {'name': 'board_read', 'arguments': '{"id": "VN69"}'}}]})
        result = self.parse(data + event(finish='tool_calls') + b'data: [DONE]\n\n')
        self.assertEqual(result['content'], 'Recording the decisions:')
        self.assertNotIn('toolu_bdrk', result['content'])
        self.assertEqual(result['tool_calls'][0]['function']['name'], 'board_read')

    def test_a_cut_off_step_keeps_no_leaked_json_in_its_partial(self):
        data = event({'content': 'Half an answer\n'})
        data += event({'content': '{"type": "tool_use", "id": "toolu_1", "name": "x"}'})
        with self.assertRaises(ProviderTruncated) as caught:
            self.parse(data + event(finish='length') + b'data: [DONE]\n\n')
        self.assertEqual(caught.exception.partial['content'], 'Half an answer')
        # A cut-off step that was nothing but the leak keeps no partial at all.
        with self.assertRaises(ProviderTruncated) as caught:
            self.parse(event({'content': '{"type": "tool_use", "id": "toolu_1"}'})
                       + event(finish='length') + b'data: [DONE]\n\n')
        self.assertIsNone(caught.exception.partial)

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


class SharedOpenerTests(unittest.TestCase):
    """The urllib opener is built once per process, not once per request (#TZWF).

    `build_opener` makes an `HTTPSHandler` whatever the scheme, and `HTTPSHandler.__init__` parses
    the machine's whole CA store — 11.8 ms and, on CPython 3.14, heap that is never returned. It
    used to happen on every model call and every side call, including calls to 127.0.0.1.
    """

    @classmethod
    def setUpClass(cls):
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args): pass
            def do_POST(self):
                self.rfile.read(int(self.headers['Content-Length']))
                self.send_response(200); self.send_header('Content-Type', 'text/event-stream'); self.end_headers()
                self.wfile.write(event({'content': 'OK'}) + event(finish='stop') + b'data: [DONE]\n\n')
        cls.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True); cls.thread.start()
        cls.base = f'http://127.0.0.1:{cls.server.server_port}/v1'

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown(); cls.server.server_close(); cls.thread.join()

    def setUp(self):
        provider_module.reset_shared_openers()
        self.addCleanup(provider_module.reset_shared_openers)

    def counted(self, requests: int) -> tuple[int, int]:
        """(openers built, CA stores parsed) for `requests` completions through the stub."""
        import ssl
        openers, certs = [], []
        real_build, real_load = urllib.request.build_opener, ssl.SSLContext.load_default_certs
        def build(*handlers):
            openers.append(1); return real_build(*handlers)
        def load(self, *args, **kwargs):
            certs.append(1); return real_load(self, *args, **kwargs)
        with mock.patch.object(urllib.request, 'build_opener', build), \
             mock.patch.object(ssl.SSLContext, 'load_default_certs', load):
            for _ in range(requests):
                provider = ChatProvider(ProviderConfig(self.base, 'test-model', 'TEST_SECRET'))
                result = provider.complete([{'role': 'user', 'content': 'hi'}], [], lambda x: None,
                                           threading.Event())
                self.assertEqual(result['content'], 'OK')
        return len(openers), len(certs)

    def test_the_ca_store_is_parsed_once_however_many_requests_are_made(self):
        self.assertEqual(self.counted(5), (1, 1))
        # …and not again for the next five: the opener outlives the provider objects.
        self.assertEqual(self.counted(5), (0, 0))

    def test_one_opener_serves_every_caller(self):
        from relay_core import customproviders, localmodels
        first = provider_module.shared_opener()
        self.assertIs(provider_module.shared_opener(), first)
        self.assertIs(customproviders.shared_opener(), first)
        self.assertIs(localmodels.shared_opener(proxies=False),
                      provider_module.shared_opener(proxies=False))
        self.assertIsNot(provider_module.shared_opener(proxies=False), first)
        for opener in (first, provider_module.shared_opener(proxies=False)):
            self.assertTrue(any(isinstance(h, provider_module.NoRedirect) for h in opener.handlers))

    def test_a_local_probe_still_goes_nowhere_near_a_proxy(self):
        with mock.patch.dict(os.environ, {'http_proxy': 'http://127.0.0.1:9/'}):
            provider_module.reset_shared_openers()
            direct = provider_module.shared_opener(proxies=False)
            proxied = provider_module.shared_opener()
        self.assertFalse(any(isinstance(h, urllib.request.ProxyHandler) for h in direct.handlers))
        self.assertTrue(any(isinstance(h, urllib.request.ProxyHandler) for h in proxied.handlers))
        # The environment is read when the opener is built, so a caller that changes it resets.
        with mock.patch.dict(os.environ):
            for name in ('http_proxy', 'HTTP_PROXY', 'https_proxy', 'HTTPS_PROXY', 'all_proxy', 'ALL_PROXY'):
                os.environ.pop(name, None)
            provider_module.reset_shared_openers()
            self.assertFalse(any(isinstance(h, urllib.request.ProxyHandler)
                                 for h in provider_module.shared_opener().handlers))

    def test_threads_sharing_the_opener_get_the_same_one(self):
        # The worker runs the model call and its side calls on different threads.
        seen, done = [], threading.Barrier(4)
        def grab():
            done.wait(); seen.append(provider_module.shared_opener())
        threads = [threading.Thread(target=grab) for _ in range(3)]
        for t in threads: t.start()
        done.wait()
        for t in threads: t.join()
        self.assertEqual(len(set(id(o) for o in seen)), 1)


class DeadlineTests(unittest.TestCase):
    """The two deadlines: the gap between chunks, and the wait for the first one (15.1)."""

    def provider(self, **kw):
        config = ProviderConfig(base_url="https://example.invalid/v1", model="m/1", api_key="k")
        return ChatProvider(config, **kw)

    def test_without_the_option_the_first_token_waits_the_idle_deadline(self):
        provider = self.provider(stall_timeout=60)
        self.assertEqual(provider.first_token_timeout, 60.0)
        self.assertEqual(provider.deadline, 60.0)                 # nothing streamed yet

    def test_the_first_token_budget_is_a_floor_under_the_idle_deadline_not_a_cap(self):
        provider = self.provider(stall_timeout=60, first_token_timeout=180)
        self.assertEqual(provider.first_token_timeout, 180.0)
        self.assertEqual(provider.deadline, 180.0)
        provider._note_progress(usable=True)                      # the answer has started
        self.assertEqual(provider.deadline, 60.0)                 # gaps keep the short deadline
        # Asking for less than the idle deadline changes nothing: it is a floor, not a cap.
        self.assertEqual(self.provider(stall_timeout=120, first_token_timeout=30).first_token_timeout, 120.0)

    def test_the_header_budget_follows_the_first_token_budget(self):
        provider = self.provider(stall_timeout=60, first_token_timeout=300)
        self.assertEqual(provider.open_timeout, 300.0)            # max(CONNECT_TIMEOUT, first token)

    def test_it_can_be_set_and_cleared_while_the_provider_lives(self):
        provider = self.provider(stall_timeout=60)
        self.assertEqual(provider.set_first_token_timeout(240), 240.0)
        self.assertEqual(provider.set_first_token_timeout(0), 60.0)   # back on the idle deadline
        with self.assertRaises(ValueError):
            provider.set_first_token_timeout(4000)

    def test_a_local_endpoint_keeps_its_own_longer_budget(self):
        config = ProviderConfig(base_url="http://127.0.0.1:8080/v1", model="m/1", api_key="",
                                local=True, first_token_timeout=300)
        provider = ChatProvider(config, stall_timeout=60)
        self.assertEqual(provider.first_token_timeout, 300.0)
        # And the larger of the two wins when the option asks for more.
        provider.set_first_token_timeout(600)
        self.assertEqual(provider.first_token_timeout, 600.0)


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
                if mount == '/switch/v1':
                    return self.refuse(429, '30')
                if mount == '/server/v1' and seen == 1:
                    return self.refuse(500)
                if mount == '/notimpl/v1':
                    return self.refuse(501)
                if mount == '/nan/v1' and seen == 1:
                    return self.refuse(429, 'nan')
                if mount == '/budget/v1':
                    return self.refuse(429, '5')
                if mount == '/creep/v1':
                    return self.refuse(429, '1')
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

    def test_a_model_switch_ends_the_retry_wait_at_once(self):
        # Card #DC4J: the agent installs `preempt_check`, which says whether a set_model is waiting
        # to land. While a Retry-After: 30 is being waited out, the answer turning True ends the
        # wait with ProviderPreempted within a tick, nothing more is sent, and nothing is left open.
        provider = self.provider('/switch/v1')
        switching = threading.Event()
        provider.preempt_check = switching.is_set
        threading.Timer(0.2, switching.set).start()
        started = time.monotonic()
        events = []
        with self.assertRaises(ProviderPreempted) as caught:
            provider.complete([{'role': 'user', 'content': 'hello'}], [], events.append, threading.Event())
        self.assertLess(time.monotonic() - started, 1.5)
        self.assertGreaterEqual(caught.exception.waited, 0.15)
        self.assertEqual((caught.exception.status, caught.exception.attempt), (429, 1))
        self.assertEqual(self.seen('/switch/v1'), 1)
        self.assertFalse(provider.response_open())
        self.assertTrue(any(e['event'] == 'provider_retry' and e['reason'] == 'http' for e in events))
        self.assertIsNone(provider._retry_origin)          # the logical call is over

    def test_a_stop_wins_over_a_model_switch_during_the_wait(self):
        provider = self.provider('/switch/v1')
        provider.preempt_check = lambda: True
        cancel = threading.Event()
        cancel.set()
        with self.assertRaises(Cancelled):
            self.complete(provider, cancel)
        self.assertFalse(provider.response_open())

    @staticmethod
    def refusal(headers: dict, status: int = 429):
        message = email.message.Message()
        for name, value in headers.items():
            message[name] = value
        return urllib.error.HTTPError('http://x/', status, 'Too Many Requests', message, None)

    def test_retry_after_header_parsing(self):
        provider = self.provider('/rate/v1')
        refusal = self.refusal
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
        # nan and inf go through float() and used to survive the clamp as themselves: nan is a hot
        # loop of zero-delay retries and a status line reading "asking again in nan s".
        for header in ('nan', 'NaN', 'inf', '-inf', 'Infinity'):
            self.assertIsNone(provider._retry_after_s(refusal({'Retry-After': header})), header)
            self.assertIsNone(provider._retry_after_s(refusal({'retry-after-ms': header})), header)
        # An unusable retry-after-ms still lets a sane Retry-After decide.
        self.assertEqual(provider._retry_after_s(
            refusal({'retry-after-ms': 'nan', 'Retry-After': '3'})), 3.0)

    def test_a_non_finite_retry_after_falls_back_to_the_backoff(self):
        provider = self.provider('/nan/v1')
        provider.HTTP_RETRY_BASE_S = provider.HTTP_RETRY_CEILING_S = 0.01
        started = time.monotonic()
        result, events = self.complete(provider)
        self.assertEqual(result['content'], 'RETRY_OK')
        self.assertEqual(self.seen('/nan/v1'), 2)
        self.assertLess(time.monotonic() - started, 5.0)
        note = next(e for e in events if e['event'] == 'provider_retry')
        self.assertNotIn('nan', note['text'])

    def test_a_status_the_endpoint_will_not_change_its_mind_about_is_not_retried(self):
        # 501 (no such route here) and 505 (not this HTTP version) are as final as a 404: only the
        # 5xx that mean "not now" are asked again.
        provider = self.provider('/notimpl/v1')
        with self.assertRaises(ProviderError) as caught:
            self.complete(provider)
        self.assertIn('501', str(caught.exception))
        self.assertEqual(self.seen('/notimpl/v1'), 1)
        for final in (501, 505, 404, 401, 400):
            self.assertNotIn(final, provider.HTTP_RETRY_STATUSES)
        for transient in (408, 409, 429, 500, 502, 503, 504, 529):
            self.assertIn(transient, provider.HTTP_RETRY_STATUSES)

    def test_a_wait_that_does_not_fit_the_budget_is_not_waited(self):
        # Six retries each honouring a Retry-After of a minute is six minutes of a pane, a side
        # call or the Test button showing nothing. The wall clock caps the loop too.
        provider = self.provider('/budget/v1')
        started = time.monotonic()
        with provider.limit_retry_budget(1.0):      # the server names 5 s: it does not fit
            with self.assertRaises(ProviderError) as caught:
                self.complete(provider)
        self.assertIn('429', str(caught.exception))
        self.assertEqual(self.seen('/budget/v1'), 1)
        self.assertLess(time.monotonic() - started, 5.0)

    def test_the_budget_counts_the_waits_already_spent(self):
        provider = self.provider('/creep/v1')
        started = time.monotonic()
        with provider.limit_retry_budget(2.5):      # two 1 s waits fit; the third does not
            with self.assertRaises(ProviderError):
                self.complete(provider)
        elapsed = time.monotonic() - started
        self.assertEqual(self.seen('/creep/v1'), 3)
        self.assertGreaterEqual(elapsed, 2.0)
        self.assertLess(elapsed, 6.0)

    def test_the_budget_is_derived_from_the_deadline_and_only_ever_narrowed(self):
        provider = self.provider('/rate/v1')
        self.assertEqual(provider.retry_budget, provider.first_token_timeout * 2)
        exc = self.refusal({'Retry-After': '10'})
        self.assertEqual(provider._http_retry_wait(exc, 1, 0.0), 10.0)
        self.assertIsNone(provider._http_retry_wait(exc, 1, provider.retry_budget - 5.0))
        with provider.limit_retry_budget(5.0):
            self.assertEqual(provider.retry_budget, 5.0)
            self.assertIsNone(provider._http_retry_wait(exc, 1, 0.0))
            with provider.limit_retry_budget(600.0):
                self.assertEqual(provider.retry_budget, 5.0)     # narrows, never widens
        self.assertEqual(provider.retry_budget, provider.first_token_timeout * 2)


class SideCallBudgetTests(unittest.TestCase):
    """A side call (a title, a recap, compaction, route_assist) streams nothing and shows no wait,
    so it runs inside a small retry budget instead of the turn's."""

    def provider(self):
        return ChatProvider(ProviderConfig('http://127.0.0.1:1234/v1', 'mock', ''))

    def test_a_side_call_narrows_the_budget_for_that_call_only(self):
        from relay_core import sidecall
        provider, seen = self.provider(), []
        with mock.patch.object(ChatProvider, 'complete',
                               side_effect=lambda *a, **k: (seen.append(provider.retry_budget),
                                                            {'content': 'title'})[1]):
            text, _ = sidecall.call(provider, 'sys', 'user')
        self.assertEqual(text, 'title')
        self.assertEqual(seen, [sidecall.RETRY_BUDGET_S])
        self.assertLess(sidecall.RETRY_BUDGET_S, provider.retry_budget)   # restored afterwards
        self.assertEqual(provider.retry_budget, provider.first_token_timeout * 2)

    def test_a_caller_may_name_its_own_budget_or_keep_the_provider_s(self):
        from relay_core import sidecall
        provider, seen = self.provider(), []
        with mock.patch.object(ChatProvider, 'complete',
                               side_effect=lambda *a, **k: (seen.append(provider.retry_budget),
                                                            {'content': 'ok'})[1]):
            sidecall.call(provider, 'sys', 'user', retry_budget_s=7.5)
            sidecall.call(provider, 'sys', 'user', retry_budget_s=None)
        self.assertEqual(seen, [7.5, provider.first_token_timeout * 2])

    def test_a_provider_double_without_the_budget_still_works(self):
        # Most callers pass a real transport; the tests around them pass a Mock.
        from relay_core import sidecall
        double = mock.Mock()
        double.complete.return_value = {'content': 'ok'}
        self.assertEqual(sidecall.call(double, 'sys', 'user')[0], 'ok')


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


class CacheCountsTests(unittest.TestCase):
    """The prefix cache, out of each shape a preset can report it in (#GMCF decision 5).

    Every sample below is the real thing: the llama.cpp ones were recorded from
    `local:bonsai` (llama.cpp b10706) on 2026-09-20 and are in
    docs/qa_evidence/2026-09-20-perf-fixes/cachetok/; the rest are the field names the
    providers' own documentation gives, as PROPOSAL.md § 4.3 lists them.
    """

    def test_openai_shape(self):
        # OpenAI, OpenRouter, Moonshot (kimi), Z.AI (glm), Gemini's OpenAI layer, llama.cpp.
        usage = {'prompt_tokens': 58, 'completion_tokens': 8, 'total_tokens': 66,
                 'prompt_tokens_details': {'cached_tokens': 54}}
        self.assertEqual(cache_counts(usage), {'cached_tokens': 54})

    def test_deepseek_shape(self):
        usage = {'prompt_tokens': 1200, 'completion_tokens': 40,
                 'prompt_cache_hit_tokens': 1088, 'prompt_cache_miss_tokens': 112}
        self.assertEqual(cache_counts(usage), {'cached_tokens': 1088})

    def test_anthropic_shape_carries_the_write_count_too(self):
        # tests/fixtures/guest_harness_claude/hello.jsonl, `result.usage`.
        usage = {'input_tokens': 10, 'output_tokens': 71,
                 'cache_creation_input_tokens': 7624, 'cache_read_input_tokens': 13689}
        self.assertEqual(cache_counts(usage),
                         {'cached_tokens': 13689, 'cache_write_tokens': 7624})

    def test_codex_shape(self):
        # tests/fixtures/guest_harness_codex/ok-turn.jsonl, as the harness maps it.
        usage = {'input_tokens': 13312, 'output_tokens': 5,
                 'cached_input_tokens': 11136, 'cache_write_input_tokens': 0}
        self.assertEqual(cache_counts(usage),
                         {'cached_tokens': 11136, 'cache_write_tokens': 0})

    def test_llama_cpp_timings_are_the_fallback_and_never_override_usage(self):
        # A build older than the one that added prompt_tokens_details reports only `timings`.
        self.assertEqual(cache_counts({'prompt_tokens': 58}, {'cache_n': 54, 'prompt_n': 4}),
                         {'cached_tokens': 54})
        # b10706 sends both, and `usage` is the one that is read.
        both = cache_counts({'prompt_tokens': 58, 'prompt_tokens_details': {'cached_tokens': 54}},
                            {'cache_n': 54})
        self.assertEqual(both, {'cached_tokens': 54})

    def test_a_provider_that_says_nothing_reports_nothing(self):
        # Never zero-for-unknown: "this provider does not report caching" is its own answer.
        self.assertEqual(cache_counts({'prompt_tokens': 58, 'completion_tokens': 8}), {})
        self.assertEqual(cache_counts({'prompt_tokens_details': {}}, None), {})
        self.assertEqual(cache_counts(None), {})
        # A cache that missed reports 0, which is a number and is kept.
        self.assertEqual(cache_counts({'prompt_tokens_details': {'cached_tokens': 0}}),
                         {'cached_tokens': 0})

    def test_nonsense_counts_are_not_counts(self):
        for bad in (True, -1, 'lots', None, {'n': 1}):
            self.assertEqual(cache_counts({'prompt_tokens_details': {'cached_tokens': bad}}), {})


class UsageEventCacheTests(unittest.TestCase):
    """The counts reach the `usage` event from both the streaming and the whole-body paths."""

    def setUp(self):
        self.provider = ChatProvider(ProviderConfig('http://127.0.0.1:1234/v1', 'mock', ''))
        self.events = []

    def usage_of(self, data):
        self.provider._stream(io.BytesIO(data), self.events.append, threading.Event())
        return [e['usage'] for e in self.events if e['event'] == 'usage'][-1]

    def test_a_streamed_final_chunk_carries_both_shapes(self):
        # The exact last chunk local:bonsai sends (llama.cpp b10706), second turn of a pair.
        final = ('data: ' + json.dumps({
            'choices': [], 'usage': {'completion_tokens': 8, 'prompt_tokens': 58, 'total_tokens': 66,
                                     'prompt_tokens_details': {'cached_tokens': 54}},
            'timings': {'cache_n': 54, 'prompt_n': 4}}) + '\n\n').encode()
        usage = self.usage_of(event({'content': 'hi'}) + event(finish='stop') + final + b'data: [DONE]\n\n')
        self.assertEqual(usage['cached_tokens'], 54)
        self.assertNotIn('cache_write_tokens', usage)
        # What the provider sent is still there, untouched.
        self.assertEqual(usage['prompt_tokens_details'], {'cached_tokens': 54})

    def test_timings_alone_still_report_the_cache(self):
        final = ('data: ' + json.dumps({
            'choices': [], 'usage': {'prompt_tokens': 58, 'completion_tokens': 8},
            'timings': {'cache_n': 54}}) + '\n\n').encode()
        usage = self.usage_of(event({'content': 'hi'}) + event(finish='stop') + final + b'data: [DONE]\n\n')
        self.assertEqual(usage['cached_tokens'], 54)

    def test_a_provider_without_a_cache_adds_no_key(self):
        final = ('data: ' + json.dumps({'choices': [],
                                        'usage': {'prompt_tokens': 58, 'completion_tokens': 8}}) + '\n\n').encode()
        usage = self.usage_of(event({'content': 'hi'}) + event(finish='stop') + final + b'data: [DONE]\n\n')
        self.assertNotIn('cached_tokens', usage)

    def test_the_whole_body_path_reports_it_too(self):
        # A non-streamed answer: llama.cpp puts `timings` beside `usage` there as well.
        body = json.dumps({'choices': [{'message': {'content': 'hi'}, 'finish_reason': 'stop'}],
                           'usage': {'prompt_tokens': 58, 'completion_tokens': 8,
                                     'prompt_tokens_details': {'cached_tokens': 54}},
                           'timings': {'cache_n': 54}}).encode()
        message = email.message.Message()
        message['Content-Type'] = 'application/json'
        response = io.BytesIO(body)
        response.headers = message
        response.__enter__ = lambda self=response: self
        response.__exit__ = lambda *args: False
        with mock.patch.object(ChatProvider, '_open', return_value=response), \
             mock.patch.object(ChatProvider, '_watch_for_stall', return_value=None):
            self.provider.complete([{'role': 'user', 'content': 'hi'}], [], self.events.append,
                                   threading.Event())
        usage = [e['usage'] for e in self.events if e['event'] == 'usage'][-1]
        self.assertEqual(usage['cached_tokens'], 54)


class CodingQuotaTests(unittest.TestCase):
    def test_spent_coding_quota_is_not_retried_and_only_reset_time_is_disclosed(self):
        import urllib.error
        from unittest import mock
        provider = ChatProvider(ProviderConfig('https://api.z.ai/api/coding/paas/v4', 'glm-5.3', 'fixture'))
        body = io.BytesIO(json.dumps({'error': {'code': '1310', 'message':
            'Weekly/Monthly Limit Exhausted. Your limit will reset at 2026-09-23 05:53:09 secret-body'}}).encode())
        error = urllib.error.HTTPError(provider.config.base_url, 429, 'quota', {}, body)
        opener = mock.Mock()
        opener.open.side_effect = error
        events = []
        with self.assertRaises(ProviderError) as caught:
            provider._open(opener, mock.Mock(), events.append, threading.Event(), time.monotonic())
        self.assertEqual(opener.open.call_count, 1)
        self.assertIn('2026-09-23 05:53:09', str(caught.exception))
        self.assertNotIn('secret-body', str(caught.exception))
        self.assertEqual(caught.exception.code, 'provider_quota_exhausted')
        self.assertTrue(body.closed)

    def test_kimi_spent_window_403_is_a_quota_refusal_not_an_auth_one(self):
        # #P004: Kimi Code answers a spent 5-hour window with 403 typed access_terminated_error.
        # The turn says which limit and is suppressed, instead of re-reading the keyring and
        # failing over on every turn until the window resets.
        import urllib.error
        from unittest import mock
        provider = ChatProvider(ProviderConfig('https://api.kimi.ai/coding/v1', 'k3', 'fixture'))
        body = io.BytesIO(json.dumps({'error': {
            'type': 'access_terminated_error',
            'message': "You've reached your 5-hour usage limit. Your quota will reset when the "
                       "current 5-hour window ends."}}).encode())
        error = urllib.error.HTTPError(provider.config.base_url, 403, 'Forbidden', {}, body)
        opener = mock.Mock()
        opener.open.side_effect = error
        events = []
        with self.assertRaises(ProviderError) as caught:
            provider._open(opener, mock.Mock(), events.append, threading.Event(), time.monotonic())
        self.assertEqual(opener.open.call_count, 1)
        self.assertEqual(caught.exception.code, 'provider_quota_exhausted')
        self.assertIn('5-hour', str(caught.exception))
        self.assertTrue(body.closed)

    def test_a_key_rejected_403_keeps_the_retry_and_failover_path(self):
        import urllib.error
        provider = ChatProvider(ProviderConfig('https://api.kimi.ai/coding/v1', 'k3', 'fixture'))
        error = urllib.error.HTTPError(provider.config.base_url, 403, 'Forbidden', {},
                                       io.BytesIO(b'{"error":{"message":"invalid key"}}'))
        self.assertIsNone(provider._coding_quota_error(error))
        # Final for the retry policy too: no in-place retries, the turn fails over at once.
        self.assertIsNone(provider._http_retry_delay(error, 1))

    def test_transient_coding_429_keeps_retry_policy(self):
        import urllib.error
        provider = ChatProvider(ProviderConfig('https://api.z.ai/api/coding/paas/v4', 'glm-5.3', 'fixture'))
        error = urllib.error.HTTPError(provider.config.base_url, 429, 'rate limit', {},
                                      io.BytesIO(b'{"error":{"code":"1302","message":"Rate limit"}}'))
        self.assertIsNone(provider._coding_quota_error(error))
        self.assertIsNotNone(provider._http_retry_delay(error, 1))


class QuotaSuppressionTests(unittest.TestCase):
    def test_repeated_calls_suppressed_then_rechecked_and_switch_can_preempt(self):
        from unittest import mock
        from relay_core.provider import ProviderQuotaExhausted, ProviderPreempted
        provider = ChatProvider(ProviderConfig('https://api.z.ai/api/coding/paas/v4', 'glm', 'fixture'))
        error = urllib.error.HTTPError(provider.config.base_url, 429, 'quota', {},
            io.BytesIO(b'{"error":{"code":"1310","message":"reset at 2030-01-01 00:00:00"}}'))
        opener = mock.Mock()
        opener.open.side_effect = [error, 'recovered']
        def call():
            return provider._open(opener, mock.Mock(), lambda e: None, threading.Event(), 100.)
        with mock.patch('relay_core.provider.time.monotonic', return_value=100.), \
             mock.patch('relay_core.provider.time.time', return_value=0):
            for _ in range(2):
                with self.assertRaises(ProviderQuotaExhausted): call()
            self.assertEqual(opener.open.call_count, 1)
            provider.preempt_check = lambda: True
            with self.assertRaises(ProviderPreempted): call()
            provider.preempt_check = None
            cancelled = threading.Event()
            cancelled.set()
            with self.assertRaises(Cancelled):
                provider._open(opener, mock.Mock(), lambda e: None, cancelled, 100.)
        with mock.patch('relay_core.provider.time.monotonic', return_value=161.):
            self.assertEqual(call(), 'recovered')
        self.assertEqual(opener.open.call_count, 2)

    def test_reset_boundary_invalid_calendar_and_transient_type(self):
        from unittest import mock
        from datetime import datetime, timezone
        provider = ChatProvider(ProviderConfig('https://api.z.ai/api/coding/paas/v4', 'glm', 'fixture'))
        def error(message):
            return urllib.error.HTTPError(provider.config.base_url, 429, 'quota', {},
                io.BytesIO(json.dumps({'error': {'code': '1310', 'message': message}}).encode()))
        # Earliest possible interpretation is 2030-01-01 00:00 UTC (provider UTC+14).
        boundary = datetime(2030, 1, 1, tzinfo=timezone.utc).timestamp()
        with mock.patch('relay_core.provider.time.time', return_value=boundary - 10):
            quota = provider._coding_quota_error(error('reset at 2030-01-01 14:00:00'))
            self.assertEqual(quota.retry_after_s, 10.)
            self.assertIsNone(quota.resets_at)
        with mock.patch('relay_core.provider.time.time', return_value=boundary):
            self.assertEqual(provider._coding_quota_error(error('reset at 2030-01-01 14:00:00')).retry_after_s, 0)
        invalid = provider._coding_quota_error(error('reset at 2030-99-99 99:99:99'))
        self.assertNotIn('2030-99', str(invalid))
        self.assertEqual(invalid.retry_after_s, 60.)
        self.assertEqual(provider._http_error(error('transient')).code, 'provider_rate_limited')

    def test_credential_change_does_not_reuse_quota_refusal(self):
        from relay_core.provider import ProviderQuotaExhausted
        from unittest import mock
        provider = ChatProvider(ProviderConfig('https://api.z.ai/api/coding/paas/v4', 'glm', 'old'))
        provider._quota_refusal = ((provider.config.base_url, 'glm', 'old'), time.monotonic()+60,
                                   ProviderQuotaExhausted('exhausted', 60))
        provider.config.api_key = 'new'
        opener = mock.Mock()
        provider._open(opener, mock.Mock(), lambda e: None, threading.Event(), time.monotonic())
        opener.open.assert_called_once()
