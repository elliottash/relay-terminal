# SPDX-License-Identifier: AGPL-3.0-or-later
"""Pane-owned board dispatcher and stdio MCP proxy (card #4NXH).

Only the worker owns BoardTools. The guest's proxy carries a private, ephemeral
capability file; it never opens a board or invents an actor. No transport retry
replays a mutation. Calls and turn teardown share a lock.
"""
from __future__ import annotations

import hmac
import copy
import json
import os
from pathlib import Path
import secrets
import socket
import socketserver
import sys

# A direct script launch must not shadow stdlib queue with relay_core/queue.py.
if __name__ == "__main__":
    sys.path[0] = str(Path(__file__).resolve().parent.parent)
import tempfile
import threading
import uuid

BOARD_ALLOW = frozenset(('board_list', 'board_read', 'board_comment',
                   'board_update_card', 'board_move_card'))
DELEGATION_ALLOW = frozenset(('agent', 'agent_message', 'agent_wait', 'update_todos'))
ALLOW = BOARD_ALLOW | DELEGATION_ALLOW
WAIT_SECONDS = 10
MAX_MESSAGE = 2 * 1024 * 1024
VERSIONS = ('2025-11-25', '2025-06-18', '2025-03-26', '2024-11-05')


def failure(code, message):
    return {'ok': False, 'error': message, 'code': code}


class Bridge:
    def __init__(self, available=False, *, delegation=True):
        self.agent = None
        self.available = available
        self.delegation = delegation
        self.ready = threading.Event()
        self.lock = threading.RLock()
        self.active = None
        self.closed = False
        self.cache = {}
        self.cancelled = set()
        self.cancel_lock = threading.Lock()
        self.tmp = tempfile.TemporaryDirectory(prefix='relay-board-')
        os.chmod(self.tmp.name, 0o700)
        self.token = secrets.token_hex(32)
        self.path = str(Path(self.tmp.name) / 'socket')
        credential = Path(self.tmp.name) / 'capability.json'
        credential.write_text(json.dumps({'socket': self.path, 'token': self.token}))
        credential.chmod(0o600)
        # Absolute script path works in source and installed backend layouts, independent
        # of the guest's cwd/PYTHONPATH. No secret in argv or persisted client config.
        self.descriptor = {'command': sys.executable,
                           'args': [str(Path(__file__).resolve()), str(credential)]}
        bridge = self

        class Handler(socketserver.StreamRequestHandler):
            def handle(self):
                self.connection.settimeout(10)
                try:
                    line = self.rfile.readline(MAX_MESSAGE + 1)
                    if len(line) > MAX_MESSAGE:
                        return
                    request = json.loads(line)
                    result = bridge.request(request)
                    self.wfile.write((json.dumps(result) + '\n').encode())
                except (OSError, ValueError, TypeError):
                    return

        class Server(socketserver.ThreadingUnixStreamServer):
            daemon_threads = True
        self.server = Server(self.path, Handler)
        self.thread = threading.Thread(target=self.server.serve_forever,
                                       kwargs={'poll_interval': .05}, daemon=True)
        self.thread.start()

    def bind(self, agent):
        with self.lock:
            self.agent = agent

    def specs(self):
        from relay_core.subagents import delegation_tool_specs
        from relay_core.todos import SPEC as TODO_SPEC
        if self.agent is not None:
            board = getattr(self.agent, 'board', None)
            specs = board.tool_specs() if board is not None else []
            manager = self.agent.subagents
            if self.delegation and manager is not None:
                specs = specs + manager.tool_specs()
            if self.delegation and self.agent._todos_enabled():
                specs = specs + [TODO_SPEC]
        else:
            from relay_core.board_tools import TOOL_SPECS
            specs = (list(TOOL_SPECS) if self.available else [])
            if self.delegation:
                specs += delegation_tool_specs() + [TODO_SPEC]
        specs = copy.deepcopy(specs)
        for spec in specs:
            f = spec['function']
            if f['name'] == 'agent':
                f['description'] += (' In a guest, launches always run in the background. '
                                     'Use agent_wait to read the result; link a Relay task with todo_id.')
                f['parameters']['properties']['background'] = {'type': 'boolean', 'enum': [True]}
            elif f['name'] == 'agent_wait':
                f['description'] += ' Guest waits return within 10 seconds; call again if still running.'
                f['parameters']['properties']['timeout_seconds']['maximum'] = WAIT_SECONDS
        return [{'name': f['name'], 'description': f.get('description', ''),
                 'inputSchema': f['parameters']}
                for spec in specs for f in [spec['function']] if f['name'] in ALLOW]

    def begin(self, cancel):
        with self.lock:
            self.active = (uuid.uuid4().hex, cancel)

    def end(self):
        with self.lock:
            self.active = None

    def request(self, request):
        if not isinstance(request, dict) or not isinstance(request.get('token'), str) \
                or not hmac.compare_digest(request['token'], self.token):
            return failure('unauthorized', 'Invalid board capability.')
        if request.get('method') == 'cancel':
            key = request.get('key')
            if isinstance(key, str) and len(key) <= 200:
                with self.cancel_lock:
                    if len(self.cancelled) < 10000:
                        self.cancelled.add(key)
            return {}
        # Capture before waiting for a preceding write: a queued call must not migrate
        # into a replacement turn after Stop or normal completion.
        active = self.active
        with self.lock:
            if self.closed:
                return failure('unavailable', 'Board bridge is closed.')
            if request.get('method') == 'tools/list':
                self.ready.set()
                return {'tools': self.specs()}
            if request.get('method') != 'tools/call':
                return failure('unknown_method', 'Unknown bridge operation.')
            key = request.get('key')
            params = request.get('params')
            if not isinstance(key, str) or len(key) > 200 or not isinstance(params, dict):
                return failure('invalid_request', 'Expected request key and tool parameters.')
            payload = json.dumps(params, sort_keys=True)
            if key in self.cache:
                old, result = self.cache[key]
                return result if old == payload else failure('request_reused', 'Request id reused with different arguments.')
            if len(self.cache) >= 10000:
                return failure('request_limit', 'Restart the guest to make more board calls.')
            def remember(result):
                self.cache[key] = (payload, result)
                return result

            if active is None or active is not self.active or active[1].is_set() or self.agent is None:
                return remember(failure('unavailable', 'Board tools require an active Relay turn.'))
            with self.cancel_lock:
                if key in self.cancelled:
                    return remember(failure('cancelled', 'Board request was cancelled before dispatch.'))
            name = params.get('name')
            if not isinstance(name, str) or name not in {s['name'] for s in self.specs()}:
                return remember(failure('unknown_tool', 'This board tool is not exposed.'))
            args = params.get('arguments', {})
            try:
                if not isinstance(args, dict):
                    raise ValueError('Tool arguments must be an object.')
                args = dict(args)
                # Never hold the bridge for an entire model turn. Background children use
                # Relay's normal manager, task links, transcripts, Stop and idle wake-ups.
                if name == 'agent':
                    args['background'] = True
                elif name == 'agent_wait':
                    timeout = args.get('timeout_seconds', WAIT_SECONDS)
                    if isinstance(timeout, bool) or not isinstance(timeout, int) or timeout < 1:
                        raise ValueError('timeout_seconds must be a positive integer.')
                    args['timeout_seconds'] = min(timeout, WAIT_SECONDS)
                # Resolve the deferred group, then use precisely the native policy path.
                from relay_core import tool_groups
                group = tool_groups.group_of(name)
                if group:
                    self.agent.loaded_tool_groups.add(group)
                # Claude reports its actual model in first-turn init, after Agent.ask
                # initially signs the board. Refresh from the live worker configuration.
                self.agent.sign_board()
                prepared = self.agent._prepare(name, args)
                result = self.agent._execute(prepared, getattr(self.agent, '_turn', None))
            except Exception as exc:
                # Cache even an ambiguous dispatch failure: no automatic write replay.
                result = failure('board_call_failed', str(exc))
            return remember(result)

    def close(self):
        with self.lock:
            if self.closed:
                return
            self.closed = True
            self.active = None
        self.server.shutdown()
        self.server.server_close()
        self.tmp.cleanup()


def exchange(capability, method, params=None, key=None):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(30)
        sock.connect(capability['socket'])
        message = dict(capability, method=method, params=params, key=key)
        sock.sendall((json.dumps(message) + '\n').encode())
        with sock.makefile('rb') as stream:
            line = stream.readline(MAX_MESSAGE + 1)
        if not line or len(line) > MAX_MESSAGE:
            raise ValueError('Board bridge response missing or too large.')
        return json.loads(line)


def proxy(path):
    capability = json.loads(Path(path).read_text())
    generation = uuid.uuid4().hex
    from concurrent.futures import ThreadPoolExecutor
    output_lock = threading.Lock()
    pool = ThreadPoolExecutor(max_workers=4)

    def write(response):
        with output_lock:
            print(json.dumps(response), flush=True)

    def call(request):
        ident = request['id']
        try:
            result = exchange(capability, 'tools/call', request.get('params'),
                              generation + ':' + json.dumps(ident))
            result = {'content': [{'type': 'text', 'text': json.dumps(result)}],
                      'isError': bool(result.get('error'))}
            write({'jsonrpc': '2.0', 'id': ident, 'result': result})
        except (ValueError, TypeError, OSError) as exc:
            write({'jsonrpc': '2.0', 'id': ident,
                   'error': {'code': -32603, 'message': str(exc)}})

    while True:
        line = sys.stdin.buffer.readline(MAX_MESSAGE + 1)
        if not line or len(line) > MAX_MESSAGE:
            pool.shutdown(wait=True)
            return
        ident = None
        try:
            request = json.loads(line)
            if not isinstance(request, dict):
                raise ValueError('Expected an object.')
            ident = request.get('id')
            method = request.get('method')
            if 'params' in request and not isinstance(request['params'], dict):
                raise ValueError('Expected object parameters.')
            if ident is not None and (isinstance(ident, bool) or not isinstance(ident, (str, int))):
                raise ValueError('Expected a string or integer request id.')
            if ident is None:
                if method == 'notifications/cancelled':
                    params = request.get('params') or {}
                    exchange(capability, 'cancel', key=generation + ':' + json.dumps(params.get('requestId')))
                continue
            if method == 'initialize':
                version = (request.get('params') or {}).get('protocolVersion')
                result = {'protocolVersion': version if version in VERSIONS else VERSIONS[0],
                          'capabilities': {'tools': {}},
                          'serverInfo': {'name': 'relay_board', 'version': '1'}}
            elif method == 'ping':
                result = {}
            elif method == 'tools/list':
                result = exchange(capability, method)
            elif method == 'tools/call':
                pool.submit(call, request)
                continue
            else:
                write({'jsonrpc': '2.0', 'id': ident,
                       'error': {'code': -32601, 'message': 'Unknown method'}})
                continue
            response = {'jsonrpc': '2.0', 'id': ident, 'result': result}
        except (ValueError, TypeError, OSError) as exc:
            response = {'jsonrpc': '2.0', 'id': ident,
                        'error': {'code': -32603, 'message': str(exc)}}
        write(response)


if __name__ == '__main__':
    proxy(sys.argv[1])
