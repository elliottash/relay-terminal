# SPDX-License-Identifier: GPL-3.0-or-later
"""Model servers on this machine: the registry file, the probe, and the worker messages
(backend/relay_core/localmodels.py, protocol section 23).

Two fake servers stand in for the real ones: a llama.cpp `llama-server` (`/health`, `/props`,
`/v1/models`, and the 503 it answers everything with while the weights load) and an Ollama
(`/api/tags`, `/api/show`). Every request either of them receives is recorded, so the tests can
prove the probe never sends an Authorization header.
"""
import json
import os
import socket
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from unittest import mock

from relay_core import localmodels as L
from relay_core import presets as P


class FakeServer:
    """`routes` maps a path to (status, JSON body); anything else is a 404."""

    def __init__(self, routes):
        self.routes = dict(routes)
        self.seen = []
        outer = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args): pass

            def answer(self):
                length = int(self.headers.get('Content-Length') or 0)
                body = self.rfile.read(length) if length else b''
                outer.seen.append({'method': self.command, 'path': self.path, 'body': body,
                                   'authorization': self.headers.get('Authorization')})
                status, payload = outer.routes.get(self.path, (404, {'error': 'not found'}))
                if status in (301, 302, 307):
                    self.send_response(status); self.send_header('Location', payload); self.end_headers(); return
                raw = json.dumps(payload).encode()
                self.send_response(status); self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(raw))); self.end_headers(); self.wfile.write(raw)
            do_GET = do_POST = answer

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.root = f'http://127.0.0.1:{self.server.server_port}'

    def close(self):
        self.server.shutdown(); self.server.server_close(); self.thread.join()


def llama_routes(n_ctx=131072, tools=True, sleeping=None, model='bonsai-2-27b', thinking=True):
    props = {'default_generation_settings': {'n_ctx': n_ctx}, 'total_slots': 1,
             'chat_template_caps': {'supports_tools': tools, 'supports_tool_calls': tools,
                                    'supports_preserve_reasoning': thinking}}
    if sleeping is not None:
        props['is_sleeping'] = sleeping
    return {'/health': (200, {'status': 'ok'}), '/props': (200, props),
            '/v1/models': (200, {'object': 'list', 'data': [{'id': model, 'object': 'model'}]})}


LOADING = {'error': {'code': 503, 'message': 'Loading model', 'type': 'unavailable_error'}}


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


class RegistryCase(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.path = Path(self.dir.name) / 'relay' / 'local-models.json'
        patch = mock.patch.dict(os.environ, {L.ENV_PATH: str(self.path)})
        patch.start()
        self.addCleanup(patch.stop)
        self.addCleanup(self.dir.cleanup)


class ProbeTests(unittest.TestCase):
    def serve(self, routes):
        server = FakeServer(routes)
        self.addCleanup(server.close)
        return server

    def test_llamacpp_reports_the_served_window_not_the_models_maximum(self):
        server = self.serve(llama_routes(n_ctx=131072))
        found = L.probe(server.root + '/v1')
        self.assertTrue(found.ok)
        self.assertEqual((found.server, found.state, found.context_window), ('llamacpp', 'ready', 131072))
        self.assertEqual(found.base_url, server.root + '/v1')
        self.assertEqual(found.models, [{'id': 'bonsai-2-27b', 'context_window': 131072,
                                         'tools': True, 'thinking': True}])
        bare = self.serve({'/props': (200, {'default_generation_settings': {'n_ctx': 4096}}),
                           '/v1/models': (200, {'data': [{'id': 'm'}]})})
        self.assertEqual(L.probe(bare.root).models, [{'id': 'm', 'context_window': 4096, 'tools': None, 'thinking': None}])

    def test_a_root_url_and_a_v1_url_name_the_same_server(self):
        server = self.serve(llama_routes())
        self.assertEqual(L.probe(server.root).to_dict(), L.probe(server.root + '/v1/').to_dict())
        self.assertEqual(L.roots('http://127.0.0.1:8080'), ('http://127.0.0.1:8080', 'http://127.0.0.1:8080/v1'))
        self.assertEqual(L.roots('http://127.0.0.1:8080/v1/'), ('http://127.0.0.1:8080', 'http://127.0.0.1:8080/v1'))

    def test_a_template_without_tools_is_reported(self):
        found = L.probe(self.serve(llama_routes(tools=False)).root)
        self.assertIs(found.models[0]['tools'], False)

    def test_loading_is_a_state_not_an_error(self):
        server = self.serve({'/health': (503, LOADING), '/props': (503, LOADING), '/v1/models': (503, LOADING)})
        found = L.probe(server.root + '/v1')
        self.assertTrue(found.ok)
        self.assertEqual((found.server, found.state, found.error), ('llamacpp', 'loading', ''))

    def test_a_sleeping_server_is_up(self):
        found = L.probe(self.serve(llama_routes(sleeping=True)).root)
        self.assertEqual((found.ok, found.state), (True, 'sleeping'))

    def test_ollama_lists_models_with_window_and_capabilities(self):
        server = self.serve({'/api/tags': (200, {'models': [
            {'name': 'qwen3:30b', 'details': {'context_length': 262144}, 'capabilities': ['completion', 'tools', 'thinking']},
            {'name': 'llava:7b', 'details': {'context_length': 4096}, 'capabilities': ['completion', 'vision']}]})})
        found = L.probe(server.root)
        self.assertEqual((found.server, found.state, found.base_url), ('ollama', 'ready', server.root + '/v1'))
        self.assertEqual(found.models, [
            {'id': 'qwen3:30b', 'context_window': 262144, 'tools': True, 'thinking': True},
            {'id': 'llava:7b', 'context_window': 4096, 'tools': False, 'thinking': False}])
        self.assertIsNone(found.context_window)        # two models, two windows: no single answer

    def test_older_ollama_is_asked_per_model(self):
        server = self.serve({'/api/tags': (200, {'models': [{'name': 'llama3:8b', 'details': {}}]}),
                             '/api/show': (200, {'capabilities': ['completion', 'tools'],
                                                 'model_info': {'llama.context_length': 8192}})})
        found = L.probe(server.root)
        self.assertEqual(found.models, [{'id': 'llama3:8b', 'context_window': 8192, 'tools': True, 'thinking': False}])
        self.assertEqual(found.context_window, 8192)
        show = [r for r in server.seen if r['path'] == '/api/show'][0]
        self.assertEqual((show['method'], json.loads(show['body'])), ('POST', {'model': 'llama3:8b'}))

    def test_lm_studio_and_vllm_are_told_apart_by_what_they_list(self):
        lms = self.serve({'/v1/models': (200, {'data': [{'id': 'qwen/qwen3-coder-30b', 'loaded_context_length': 32768,
                                                          'max_context_length': 262144}]})})
        vllm = self.serve({'/v1/models': (200, {'data': [{'id': 'Qwen/Qwen3-32B', 'max_model_len': 40960}]})})
        plain = self.serve({'/v1/models': (200, {'data': [{'id': 'm'}]})})
        self.assertEqual((L.probe(lms.root).server, L.probe(lms.root).context_window), ('lmstudio', 32768))
        self.assertEqual((L.probe(vllm.root).server, L.probe(vllm.root).context_window), ('vllm', 40960))
        self.assertEqual((L.probe(plain.root).server, L.probe(plain.root).context_window), ('openai-compatible', None))

    def test_nothing_listening_says_how_to_start_it(self):
        port = free_port()
        found = L.probe(f'http://127.0.0.1:{port}/v1')
        self.assertFalse(found.ok)
        self.assertEqual(found.state, 'down')
        self.assertIn(f'127.0.0.1:{port}', found.error)
        self.assertIn('llama-server', found.error)
        self.assertIn('ollama serve', L.start_hint('http://127.0.0.1:11434/v1'))
        self.assertIn('lms server start', L.start_hint('http://localhost:1234/v1'))
        self.assertIn('vllm serve', L.start_hint('http://localhost:8000/v1'))

    def test_something_else_on_the_port_is_not_a_model_server(self):
        found = L.probe(self.serve({'/': (200, {'hello': 'world'})}).root)
        self.assertFalse(found.ok)
        self.assertIn('not as a model server', found.error)

    def test_only_this_machine_is_probed(self):
        for url in ('http://example.com/v1', 'https://127.0.0.1:8080/v1', 'http://192.168.1.5:8080/v1', '', None):
            with mock.patch.object(L, '_get', side_effect=AssertionError('no request may be made')):
                found = L.probe(url)
            self.assertFalse(found.ok, url)
            self.assertIn('on this machine', found.error)

    def test_no_authorization_header_and_no_redirect_is_followed(self):
        server = self.serve(llama_routes())
        with mock.patch.dict(os.environ, {'RELAY_OPENAI_API_KEY': 'SECRET'}):
            L.probe(server.root)
        self.assertTrue(server.seen)
        self.assertEqual({r['authorization'] for r in server.seen}, {None})
        trap = self.serve({'/stolen': (200, llama_routes()['/props'][1])})
        bouncer = self.serve({'/props': (307, trap.root + '/stolen'), '/api/tags': (307, trap.root + '/stolen'),
                              '/v1/models': (307, trap.root + '/stolen')})
        self.assertFalse(L.probe(bouncer.root).ok)
        self.assertEqual(trap.seen, [])


class RegistryTests(RegistryCase):
    SPEC = {'id': 'bonsai', 'label': 'Bonsai 2 27B', 'base_url': 'http://127.0.0.1:8080/v1/',
            'model': 'bonsai-2-27b', 'server': 'llamacpp', 'context_window': 131072}

    def test_save_find_match_delete(self):
        self.assertEqual(L.catalog(), {})
        endpoint = L.save(self.SPEC)
        self.assertEqual((endpoint.id, endpoint.base_url), ('local:bonsai', 'http://127.0.0.1:8080/v1'))
        self.assertEqual(L.find('local:bonsai'), endpoint)
        self.assertIsNone(L.find('bonsai'))
        self.assertEqual(L.match('http://127.0.0.1:8080/v1'), endpoint)
        self.assertIsNone(L.match('http://127.0.0.1:9999/v1'))
        self.assertTrue(L.delete('local:bonsai'))
        self.assertFalse(L.delete('local:bonsai'))
        self.assertEqual(L.catalog(), {})

    def test_another_workers_write_is_seen(self):
        L.save(self.SPEC)
        self.assertEqual(list(L.catalog()), ['local:bonsai'])
        raw = json.loads(self.path.read_text())
        raw['endpoints'].append({**self.SPEC, 'id': 'local:second', 'model': 'other'})
        self.path.write_text(json.dumps(raw) + '\n\n')       # a different size: no reliance on mtime alone
        self.assertEqual(sorted(L.catalog()), ['local:bonsai', 'local:second'])

    def test_one_bad_entry_does_not_hide_the_rest_and_a_bad_file_is_empty(self):
        self.path.parent.mkdir(parents=True)
        self.path.write_text(json.dumps({'endpoints': [{'base_url': 'https://evil.example/v1', 'model': 'x'}, self.SPEC]}))
        self.assertEqual(list(L.catalog()), ['local:bonsai'])
        self.path.write_text('{not json')
        self.assertEqual(L.catalog(), {})

    def test_what_is_refused(self):
        bad = [{'base_url': 'https://127.0.0.1:8080/v1'}, {'base_url': 'http://example.com/v1'},
               {'base_url': 'http://user:pw@127.0.0.1:8080/v1'}, {'model': ''}, {'server': 'mystery'},
               {'context_window': 100}, {'context_window': True}, {'first_token_timeout': 0},
               {'tools': 'yes'}, {'extra': []}, {'id': '!!!'}]
        for change in bad:
            with self.assertRaises(ValueError, msg=change):
                L.from_dict({**self.SPEC, **change})

    def test_defaults_are_the_cautious_ones(self):
        endpoint = L.from_dict({'base_url': 'http://localhost:1234/v1', 'model': 'Qwen/Qwen3 Coder'})
        self.assertEqual(endpoint.id, 'local:qwen-qwen3-coder')
        self.assertEqual(endpoint.context_window, L.FALLBACK_CONTEXT_WINDOW)
        self.assertEqual((endpoint.parallel_tool_calls, endpoint.tool_text_recovery), (False, False))
        self.assertEqual(endpoint.first_token_timeout, 300.0)

    def test_an_id_can_never_reach_the_keyring(self):
        from relay_core import keystore
        with self.assertRaises(ValueError):
            keystore.lookup(L.save(self.SPEC).id)

    def test_keyless_is_about_the_url_not_about_a_missing_key(self):
        L.save(self.SPEC)
        self.assertTrue(L.keyless('local:bonsai'))
        self.assertTrue(L.keyless(None, 'http://localhost:11434/v1'))
        self.assertTrue(L.keyless('', 'http://[::1]:8080/v1'))
        for url in ('https://127.0.0.1:8080/v1', 'https://api.openai.com/v1', 'http://10.0.0.2:8080/v1', ''):
            self.assertFalse(L.keyless(None, url), url)
        self.assertFalse(L.keyless('openai', ''))

    def test_the_rest_of_the_backend_sees_a_preset(self):
        L.save(self.SPEC)
        preset = P.resolve_preset('local:bonsai')
        self.assertEqual((preset.id, preset.local, preset.server, preset.group), ('local:bonsai', True, 'llamacpp', 'local'))
        self.assertEqual(P.context_window_for(preset), 131072)
        self.assertEqual(preset.effort_style, 'none')
        self.assertEqual(P.resolve_preset(None, 'http://127.0.0.1:8080/v1', 'anything').id, 'local:bonsai')
        self.assertIsNone(P.match_preset('http://127.0.0.1:8080/v1'))    # cloud only: its id names a keyring entry
        self.assertIsNone(P.resolve_preset(None, 'http://127.0.0.1:1/v1'))
        self.assertNotIn('local:bonsai', P.PRESETS)
        self.assertEqual(P.resolve_preset('openai').id, 'openai')       # a cloud preset is untouched
        row = L.find('local:bonsai').to_dict()
        self.assertTrue(set(P.PRESETS['openai'].to_dict()) <= set(row))
        self.assertEqual((row['local'], row['server'], row['efforts']), (True, 'llamacpp', []))

    def test_detect_fills_what_the_server_knows(self):
        server = FakeServer(llama_routes(n_ctx=65536))
        self.addCleanup(server.close)
        filled, found = L.detect({'id': 'bonsai', 'base_url': server.root, 'context_window': 4096})
        self.assertTrue(found.ok)
        self.assertEqual((filled['base_url'], filled['model'], filled['server']), (server.root + '/v1', 'bonsai-2-27b', 'llamacpp'))
        self.assertEqual(filled['context_window'], 65536)      # the served window is a fact
        self.assertIs(filled['tools'], True)
        kept, _ = L.detect({'base_url': server.root, 'model': 'mine', 'tools': False})
        self.assertEqual((kept['model'], kept['tools']), ('mine', False))
        untouched, down = L.detect({'base_url': f'http://127.0.0.1:{free_port()}/v1', 'model': 'm'})
        self.assertFalse(down.ok)
        self.assertEqual(untouched['model'], 'm')


class HandlerTests(RegistryCase):
    def collect(self, request):
        events = []
        thread = L.handle(request, events.append)
        if thread is not None:
            thread.join(10)
        return events

    def test_each_request_gets_exactly_one_event(self):
        server = FakeServer(llama_routes())
        self.addCleanup(server.close)
        probed = self.collect({'type': 'local_probe', 'id': 1, 'base_url': server.root})
        self.assertEqual([(e['event'], e['id'], e['ok'], e['server']) for e in probed], [('local_probed', 1, True, 'llamacpp')])
        saved = self.collect({'type': 'local_endpoint_save', 'id': 2, 'detect': True,
                              'endpoint': {'id': 'bonsai', 'base_url': server.root}})
        self.assertEqual([e['event'] for e in saved], ['local_endpoint_saved'])
        self.assertEqual((saved[0]['endpoint']['id'], saved[0]['endpoint']['context_window']), ('local:bonsai', 131072))
        listed = self.collect({'type': 'local_endpoints', 'id': 3})
        self.assertEqual([i['id'] for i in listed[0]['items']], ['local:bonsai'])
        gone = self.collect({'type': 'local_endpoint_delete', 'id': 4, 'endpoint_id': 'local:bonsai'})
        self.assertEqual(gone, [{'event': 'local_endpoint_deleted', 'id': 4, 'endpoint_id': 'local:bonsai', 'removed': True}])

    def test_a_detect_that_finds_nothing_is_an_error_event_not_a_crash(self):
        port = free_port()
        events = self.collect({'type': 'local_endpoint_save', 'id': 5, 'detect': True,
                               'endpoint': {'base_url': f'http://127.0.0.1:{port}/v1'}})
        self.assertEqual([(e['event'], e['id']) for e in events], [('error', 5)])
        self.assertIn(f'127.0.0.1:{port}', events[0]['text'])       # the start hint, not "needs a model id"
        self.assertEqual(L.catalog(), {})

    def test_detect_waits_for_a_server_that_is_loading(self):
        server = FakeServer({'/props': (503, LOADING)})
        self.addCleanup(server.close)

        def finish_loading():
            server.routes.update(llama_routes(n_ctx=65536))
        timer = threading.Timer(0.3, finish_loading)
        timer.start()
        self.addCleanup(timer.cancel)
        with mock.patch.object(L, 'LOADING_POLL_S', 0.05):
            filled, found = L.detect({'base_url': server.root}, wait=10)
            self.assertEqual((found.state, filled['model'], filled['context_window']), ('ready', 'bonsai-2-27b', 65536))
            stuck = FakeServer({'/props': (503, LOADING)})
            self.addCleanup(stuck.close)
            _, still = L.detect({'base_url': stuck.root}, wait=0.2)
        self.assertFalse(still.ok)
        self.assertIn('still loading', still.error)
        with self.assertRaises(ValueError):
            L.handle({'type': 'local_endpoint_save', 'endpoint': {'base_url': 'https://x.example/v1', 'model': 'm'}}, lambda e: None)


if __name__ == '__main__':
    unittest.main()
