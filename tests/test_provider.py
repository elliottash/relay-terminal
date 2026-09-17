import io
import json
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from relay_core.provider import ChatProvider, ProviderConfig, ProviderError, Cancelled


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
        self.assertFalse(any(e.get('text') == 'some provider reasoning' for e in self.events))

    def test_multiple_tool_calls(self):
        data = event({'tool_calls':[
            {'index':1,'id':'b','function':{'name':'read_file','arguments':'{"path":"b"}'}},
            {'index':0,'id':'a','function':{'name':'read_file','arguments':'{"path":"a"}'}}]})
        result = self.parse(data + event(finish='tool_calls') + b'data: [DONE]\n\n')
        self.assertEqual([c['id'] for c in result['tool_calls']], ['a', 'b'])

    def test_truncated_stream_rejected(self):
        with self.assertRaises(ProviderError): self.parse(event({'content':'unfinished'}))
        with self.assertRaises(ProviderError): self.parse(event(finish='length') + b'data: [DONE]\n\n')

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

    def test_json_fallback(self):
        self.assertEqual(self.complete('/json')['content'], 'JSON_OK')

    def test_redirect_not_followed(self):
        with self.assertRaises(ProviderError): self.complete('/redirect')

    def test_http_errors_do_not_echo_provider_body(self):
        with self.assertRaises(ProviderError) as ctx: self.complete('/error')
        self.assertNotIn('SECRET_ECHO', str(ctx.exception))
        self.assertIn('401', str(ctx.exception))


class OpenRouterReasoningTests(unittest.TestCase):
    def test_reasoning_field_is_preserved_and_not_displayed(self):
        provider = ChatProvider(ProviderConfig('http://127.0.0.1:1234/v1', 'mock', '', {'reasoning': {'effort': 'high'}}))
        events = []
        data = b': OPENROUTER PROCESSING\n\n' + event({'reasoning': 'thinking hard'})
        data += event({'content': 'Done.'}) + event(finish='stop') + b'data: [DONE]\n\n'
        result = provider._stream(io.BytesIO(data), events.append, threading.Event())
        self.assertEqual(result['reasoning'], 'thinking hard')
        self.assertEqual(result['content'], 'Done.')
        self.assertFalse(any(e.get('text') == 'thinking hard' for e in events))
        self.assertTrue(any(e.get('event') == 'status' for e in events))
