# SPDX-License-Identifier: GPL-3.0-or-later
"""The transport against a model server on this machine (ProviderConfig.local), and the proof that
a hosted provider is treated exactly as before.

Shapes here are the ones measured against llama-server on 2026-09-18 (card #24XJ): `arguments` as a
string, an id on the first tool-call delta only, reasoning in `reasoning_content`, a final
`choices: []` chunk carrying `usage`.
"""
import json
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from relay_core.provider import (ChatProvider, ProviderConfig, ProviderError, ProviderStalled,
                                 loopback_http, repair_tool_calls)

TOOLS = [{'type': 'function', 'function': {'name': 'run_command', 'description': '', 'parameters': {
    'type': 'object', 'properties': {'command': {'type': 'string'}}, 'required': ['command']}}}]
CALL = {'name': 'run_command', 'arguments': {'command': 'ls'}}


def sse(delta=None, finish=None, **top):
    return b'data: ' + json.dumps({'choices': [{'delta': delta or {}, 'finish_reason': finish}], **top}).encode() + b'\n\n'


DONE = b'data: [DONE]\n\n'


class Scripted:
    """A server whose next answers are queued: (status, content type, body bytes, delay before the body)."""

    def __init__(self):
        self.queue, self.requests = [], []
        outer = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args): pass

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                outer.requests.append({'body': body, 'authorization': self.headers.get('Authorization')})
                status, kind, payload, delay = outer.queue.pop(0)
                self.send_response(status); self.send_header('Content-Type', kind); self.end_headers()
                self.wfile.flush()
                if delay:
                    time.sleep(delay)
                try:
                    self.wfile.write(payload); self.wfile.flush()
                except OSError:
                    pass

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f'http://127.0.0.1:{self.server.server_port}/v1'

    def stream(self, *chunks, delay=0):
        self.queue.append((200, 'text/event-stream', b''.join(chunks), delay))

    def fail(self, status, body):
        self.queue.append((status, 'application/json', json.dumps(body).encode(), 0))

    def close(self):
        self.server.shutdown(); self.server.server_close(); self.thread.join()


class LocalTransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = Scripted()

    @classmethod
    def tearDownClass(cls):
        cls.server.close()

    def setUp(self):
        self.server.queue.clear(); self.server.requests.clear()
        self.events = []

    def provider(self, local=True, stall=60.0, **fields):
        config = ProviderConfig(self.server.base, 'bonsai-2-27b', '', {}, 4096, local=local, **fields)
        return ChatProvider(config, stall)

    def ask(self, provider, tools=TOOLS, messages=None):
        return provider.complete(messages or [{'role': 'user', 'content': 'hi'}], tools,
                                 self.events.append, threading.Event())

    def text(self, kind):
        return ''.join(e.get('text', '') for e in self.events if e['event'] == kind)

    # ----- the request -----------------------------------------------------------------------
    def test_a_local_request_asks_for_usage_and_one_call_at_a_time(self):
        self.server.stream(sse({'content': 'ok'}), sse(finish='stop'), DONE)
        self.ask(self.provider())
        body = self.server.requests[-1]['body']
        self.assertEqual(body['stream_options'], {'include_usage': True})
        self.assertIs(body['parallel_tool_calls'], False)
        self.assertIsNone(self.server.requests[-1]['authorization'])

    def test_parallel_calls_are_the_endpoints_choice_and_need_tools(self):
        self.server.stream(sse({'content': 'ok'}), sse(finish='stop'), DONE)
        self.ask(self.provider(parallel_tool_calls=True))
        self.assertNotIn('parallel_tool_calls', self.server.requests[-1]['body'])
        self.server.stream(sse({'content': 'ok'}), sse(finish='stop'), DONE)
        self.ask(self.provider(), tools=[])
        self.assertNotIn('parallel_tool_calls', self.server.requests[-1]['body'])

    # ----- tool-call arguments as objects (a chat template that cannot parse a JSON string) ----
    def history(self, arguments='{"command": "ls"}'):
        return [{'role': 'user', 'content': 'hi'},
                {'role': 'assistant', 'content': None, 'relay_kind': 'turn',
                 'tool_calls': [{'id': 'c1', 'type': 'function',
                                 'function': {'name': 'run_command', 'arguments': arguments}}]},
                {'role': 'tool', 'tool_call_id': 'c1', 'content': 'a.txt'}]

    def replay(self, provider, messages):
        self.server.stream(sse({'content': 'ok'}), sse(finish='stop'), DONE)
        self.ask(provider, messages=messages)
        return self.server.requests[-1]['body']['messages']

    def test_arguments_travel_as_objects_only_when_the_endpoint_asks(self):
        messages = self.history()
        sent = self.replay(self.provider(), messages)
        self.assertEqual(sent[1]['tool_calls'][0]['function']['arguments'], '{"command": "ls"}')
        sent = self.replay(self.provider(tool_arguments_as_object=True), messages)
        self.assertEqual(sent[1]['tool_calls'][0]['function']['arguments'], {'command': 'ls'})
        self.assertNotIn('relay_kind', sent[1])         # the bookkeeping keys still go
        # The conversation Relay keeps is the one it replays next turn: only the wire copy changed.
        self.assertEqual(messages[1]['tool_calls'][0]['function']['arguments'], '{"command": "ls"}')
        self.assertIsNone(messages[1].get('content'))
        self.assertEqual(messages[1]['relay_kind'], 'turn')

    def test_a_hosted_provider_never_gets_objects_however_it_is_configured(self):
        with self.assertRaises(ValueError):
            ProviderConfig('https://api.example.com/v1', 'm', 'key', tool_arguments_as_object=True).validate()
        config = ProviderConfig(self.server.base, 'm', '', {}, 4096, local=False)
        config.tool_arguments_as_object = True
        with self.assertRaises(ValueError):
            config.validate()

    def test_arguments_that_are_not_json_are_left_as_they_are(self):
        for arguments in ('not json at all', '"a string"', '[1, 2]', ''):
            sent = self.replay(self.provider(tool_arguments_as_object=True), self.history(arguments))
            self.assertEqual(sent[1]['tool_calls'][0]['function']['arguments'], arguments, arguments)

    def test_a_hosted_request_is_byte_for_byte_what_it_was(self):
        self.server.stream(sse({'content': 'ok'}), sse(finish='stop'), DONE)
        self.ask(self.provider(local=False))
        self.assertEqual(sorted(self.server.requests[-1]['body']), ['max_tokens', 'messages', 'model', 'stream', 'tools'])

    def test_local_means_loopback_http_and_nothing_else(self):
        for url in ('https://127.0.0.1:8080/v1', 'https://api.example.com/v1'):
            with self.assertRaises(ValueError):
                ProviderConfig(url, 'm', 'key', local=True).validate()
        self.assertTrue(loopback_http('http://localhost:11434/v1'))
        self.assertTrue(loopback_http('http://[::1]:8080'))
        for url in ('https://localhost/v1', 'http://192.168.0.2:8080/v1', 'http://localhost.evil.example/v1', '', None):
            self.assertFalse(loopback_http(url), url)

    # ----- what llama-server really sends ----------------------------------------------------
    def test_the_measured_llama_server_stream(self):
        self.server.stream(
            sse({'reasoning_content': 'The user wants '}), sse({'reasoning_content': 'a listing.'}),
            sse({'tool_calls': [{'index': 0, 'id': 'Gq6zA7CG4FTrSW0CawIIMrG9IGLJYQzg', 'type': 'function',
                                 'function': {'name': 'run_command', 'arguments': '{'}}]}),
            sse({'tool_calls': [{'index': 0, 'function': {'arguments': '"command":"ls -la"}'}}]}),
            sse(finish='tool_calls'),
            b'data: ' + json.dumps({'choices': [], 'usage': {'prompt_tokens': 2504, 'completion_tokens': 117}}).encode() + b'\n\n',
            DONE)
        message = self.ask(self.provider())
        call = message['tool_calls'][0]
        self.assertEqual(call['id'], 'Gq6zA7CG4FTrSW0CawIIMrG9IGLJYQzg')
        self.assertEqual(json.loads(call['function']['arguments']), {'command': 'ls -la'})
        self.assertEqual(message['reasoning_content'], 'The user wants a listing.')
        self.assertEqual(self.text('thinking_delta'), 'The user wants a listing.')
        self.assertEqual([e['usage']['prompt_tokens'] for e in self.events if e['event'] == 'usage'], [2504])

    # ----- reasoning written into the answer -------------------------------------------------
    def test_think_tags_become_reasoning_even_when_cut_by_a_chunk(self):
        self.server.stream(sse({'content': '<thi'}), sse({'content': 'nk>look at the '}), sse({'content': 'files</th'}),
                           sse({'content': 'ink>There are two.'}), sse(finish='stop'), DONE)
        message = self.ask(self.provider())
        self.assertEqual(message['content'], 'There are two.')
        self.assertEqual(message['reasoning_content'], 'look at the files')
        self.assertEqual(self.text('delta'), 'There are two.')
        self.assertEqual(self.text('thinking_delta'), 'look at the files')
        self.assertIn('thinking_done', [e['event'] for e in self.events])

    def test_an_unclosed_tag_and_a_closer_without_an_opener(self):
        self.server.stream(sse({'content': 'Sure. <think>first I should'}), sse(finish='stop'), DONE)
        message = self.ask(self.provider())
        self.assertEqual((message['content'], message['reasoning_content']), ('Sure. ', 'first I should'))
        self.server.stream(sse({'content': 'the template opened it</think>\n\nDone.'}), sse(finish='stop'), DONE)
        message = self.ask(self.provider())
        self.assertEqual((message['content'], message['reasoning_content']), ('Done.', 'the template opened it'))

    def test_a_hosted_providers_tags_are_left_alone(self):
        self.server.stream(sse({'content': '<think>x</think>y'}), sse(finish='stop'), DONE)
        self.assertEqual(self.ask(self.provider(local=False))['content'], '<think>x</think>y')

    # ----- envelope repair -------------------------------------------------------------------
    def test_envelope_quirks_are_repaired_for_a_local_server_only(self):
        self.assertEqual(repair_tool_calls([{'function': CALL}, {'id': 'a', 'type': 'function', 'function': {'name': 'f'}},
                                            {'id': 'a', 'type': 'function', 'function': {'name': 'g', 'arguments': '{}'}}]),
                         [{'id': 'call_0', 'type': 'function', 'function': {'name': 'run_command', 'arguments': '{"command": "ls"}'}},
                          {'id': 'a', 'type': 'function', 'function': {'name': 'f', 'arguments': '{}'}},
                          {'id': 'call_2', 'type': 'function', 'function': {'name': 'g', 'arguments': '{}'}}])
        reply = json.dumps({'choices': [{'finish_reason': 'tool_calls', 'message': {'role': 'assistant', 'content': None,
                                                                                    'tool_calls': [{'function': CALL}]}}]}).encode()
        self.server.queue.append((200, 'application/json', reply, 0))
        call = self.ask(self.provider())['tool_calls'][0]
        self.assertEqual((call['id'], call['type'], json.loads(call['function']['arguments'])), ('call_0', 'function', {'command': 'ls'}))
        self.server.queue.append((200, 'application/json', reply, 0))
        with self.assertRaises(ProviderError):
            self.ask(self.provider(local=False))

    def test_what_cannot_be_repaired_is_still_refused(self):
        for calls in ([{'id': 'a', 'type': 'function', 'function': {'arguments': '{}'}}],      # no name
                      [{'id': 'a', 'type': 'retrieval', 'function': CALL}], ['not a call']):
            with self.assertRaises(ProviderError, msg=calls):
                ChatProvider._normalize({'tool_calls': repair_tool_calls(calls)})

    # ----- tool calls written as text --------------------------------------------------------
    def test_text_recovery_is_off_unless_the_endpoint_asks(self):
        text = '<tool_call>\n{"name": "run_command", "arguments": {"command": "ls"}}\n</tool_call>'
        self.server.stream(sse({'content': text}), sse(finish='stop'), DONE)
        message = self.ask(self.provider())
        self.assertNotIn('tool_calls', message)
        self.assertEqual(message['content'], text)
        self.server.stream(sse({'content': text}), sse(finish='stop'), DONE)
        message = self.ask(self.provider(tool_text_recovery=True))
        self.assertEqual(message['content'], '')
        self.assertEqual(json.loads(message['tool_calls'][0]['function']['arguments']), {'command': 'ls'})

    def test_recovery_never_fires_on_an_answer_or_without_tools(self):
        prose = 'Run this:\n```json\n{"name": "run_command", "arguments": {"command": "rm -rf build"}}\n```'
        self.server.stream(sse({'content': prose}), sse(finish='stop'), DONE)
        self.assertNotIn('tool_calls', self.ask(self.provider(tool_text_recovery=True)))
        bare = '{"name": "run_command", "arguments": {"command": "ls"}}'
        self.server.stream(sse({'content': bare}), sse(finish='stop'), DONE)
        self.assertNotIn('tool_calls', self.ask(self.provider(tool_text_recovery=True), tools=[]))

    # ----- deadlines -------------------------------------------------------------------------
    def test_a_slow_first_token_is_not_a_stall_for_a_local_server(self):
        self.server.stream(sse({'content': 'loaded'}), sse(finish='stop'), DONE, delay=2.5)
        message = self.ask(self.provider(stall=1.0, first_token_timeout=30.0))
        self.assertEqual(message['content'], 'loaded')

    def test_the_same_wait_is_a_stall_for_a_hosted_provider(self):
        self.server.stream(sse({'content': 'late'}), sse(finish='stop'), DONE, delay=2.5)
        with self.assertRaises(ProviderStalled):
            self.ask(self.provider(local=False, stall=1.0))

    def test_the_first_token_budget_is_not_forever(self):
        self.server.stream(sse({'content': 'never'}), sse(finish='stop'), DONE, delay=4.0)
        started = time.monotonic()
        with self.assertRaises(ProviderStalled) as caught:
            self.ask(self.provider(stall=1.0, first_token_timeout=1.5))
        self.assertLess(time.monotonic() - started, 3.5)
        self.assertEqual(caught.exception.seconds, 1.5)
        self.assertEqual(self.provider(stall=60.0, first_token_timeout=5.0).first_token_timeout, 60.0)   # never below idle
        self.assertEqual(self.provider(local=False, stall=60.0).first_token_timeout, 60.0)

    # ----- a server that is loading, full, or absent -----------------------------------------
    def test_a_loading_server_is_waited_for(self):
        loading = {'error': {'code': 503, 'message': 'Loading model', 'type': 'unavailable_error'}}
        self.server.fail(503, loading); self.server.fail(503, loading)
        self.server.stream(sse({'content': 'ready now'}), sse(finish='stop'), DONE)
        provider = self.provider()
        provider.LOADING_RETRY_S = 0.05
        self.assertEqual(self.ask(provider)['content'], 'ready now')
        self.assertEqual(len(self.server.requests), 3)
        self.assertEqual([e['text'] for e in self.events if e['event'] == 'status'], ['The local server is loading its model…'])

    def test_a_hosted_503_is_asked_again(self):
        # The local wait above is for a server still loading its weights; a hosted provider's 503
        # is a transient refusal, and since card #VMZP it is asked again like any other.
        self.server.fail(503, {'error': 'overloaded'})
        self.server.stream(sse({'content': 'back up'}), sse(finish='stop'), DONE)
        provider = self.provider(local=False)
        provider.HTTP_RETRY_BASE_S = provider.HTTP_RETRY_CEILING_S = 0.01
        self.assertEqual(self.ask(provider)['content'], 'back up')
        self.assertEqual(len(self.server.requests), 2)

    def test_overflow_is_named_and_the_body_is_never_quoted(self):
        body = {'error': {'code': 400, 'type': 'exceed_context_size_error', 'n_ctx': 8192,
                          'message': 'the request exceeds the available context size SECRET_PROMPT_ECHO'}}
        self.server.fail(400, body)
        with self.assertRaises(ProviderError) as caught:
            self.ask(self.provider(context_window=8192))
        text = str(caught.exception)
        self.assertIn('no longer fits', text)
        self.assertIn('8,192', text)
        self.assertIn('/compact', text)
        self.assertNotIn('SECRET_PROMPT_ECHO', text)
        self.server.fail(400, {'error': {'message': 'unknown field SECRET_PROMPT_ECHO'}})
        with self.assertRaises(ProviderError) as caught:
            self.ask(self.provider())
        self.assertIn('Provider HTTP 400', str(caught.exception))
        self.assertNotIn('SECRET_PROMPT_ECHO', str(caught.exception))

    def test_nothing_listening_says_how_to_start_it(self):
        import socket
        with socket.socket() as s:
            s.bind(('127.0.0.1', 0)); port = s.getsockname()[1]
        provider = ChatProvider(ProviderConfig(f'http://127.0.0.1:{port}/v1', 'm', '', local=True))
        with self.assertRaises(ProviderError) as caught:
            self.ask(provider)
        self.assertIn(f'127.0.0.1:{port}', str(caught.exception))
        self.assertIn('llama-server', str(caught.exception))


if __name__ == '__main__':
    unittest.main()
