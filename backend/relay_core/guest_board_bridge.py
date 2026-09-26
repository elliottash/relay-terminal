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

from relay_core.board_tools import TOOL_NAMES as BOARD_NAMES, CLEANUP_TOOL_NAMES
from relay_core.app_tools import TOOL_NAMES as APP_NAMES
from relay_core.activity_tools import TOOL_NAMES as ACTIVITY_NAMES
from relay_core.media import TOOL_NAMES as MEDIA_NAMES, TOOL_SPECS as MEDIA_SPECS
from relay_core.workspace_plugins import LONG_TOOLS as PLUGIN_LONG_TOOLS

BOARD_ALLOW = frozenset(BOARD_NAMES + CLEANUP_TOOL_NAMES + ('search_files',))
DELEGATION_ALLOW = frozenset(('agent', 'agent_message', 'agent_wait', 'agent_set_model', 'update_todos'))
REMOTE_ALLOW = frozenset(("run_command", "read_file", "list_directory", "write_file", "edit_file"))
EXEC_ALLOW = REMOTE_ALLOW | frozenset(("run_in_terminal", "command_output", "stop_command"))
TERMINAL_CONTEXT_ALLOW = frozenset(("terminal_history", "terminal_read"))
# All Relay-owned capabilities use the same Agent prepare/execute path as a native pane.
# Catalogs, scopes and per-turn grants still decide which calls are allowed.
APP_ALLOW = frozenset(APP_NAMES)
ACTIVITY_ALLOW = frozenset(ACTIVITY_NAMES)
CONDITIONAL_ALLOW = frozenset(('set_keybinding', 'type_into_program'))
PLAN_ALLOW = frozenset(("write_plan", "exit_plan_mode"))
ALLOW = (BOARD_ALLOW | DELEGATION_ALLOW | EXEC_ALLOW | TERMINAL_CONTEXT_ALLOW
         | APP_ALLOW | ACTIVITY_ALLOW | CONDITIONAL_ALLOW | PLAN_ALLOW | MEDIA_NAMES)
WAIT_SECONDS = 10
# Foreground children can run longer than the native 1800-second wait/test ceiling.
# The transport deadline is generous; Stop still revokes the live call promptly.
LONG_CALL_SECONDS = 86400
MAX_MESSAGE = 2 * 1024 * 1024
VERSIONS = ('2025-11-25', '2025-06-18', '2025-03-26', '2024-11-05')
# How often the proxy asks whether the offered tools changed (a task-plugin workspace activated
# or deactivated in the pane, protocol 36), to tell the guest with `notifications/tools/list_changed`.
LIST_POLL_SECONDS = 2.0


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
        # Relay's own state, not agent scratch: the post-turn sweep leaves it alone (#27AR).
        from relay_core import scratch
        scratch.mark_relay_owned(self.tmp.name)
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
            for side in (getattr(self.agent, 'app', None), getattr(self.agent, 'activity', None)):
                if side is not None:
                    specs += side.tool_specs()
            specs += MEDIA_SPECS
            catalog = self.agent.executor.keybindings
            if catalog is not None:
                specs.append(catalog.tool_spec())
        else:
            from relay_core.board_tools import TOOL_SPECS
            from relay_core.app_tools import TOOL_SPECS as APP_SPECS
            from relay_core.activity_tools import TOOL_SPECS as ACTIVITY_SPECS
            specs = (list(TOOL_SPECS) if self.available else [])
            if self.delegation:
                specs += delegation_tool_specs() + [TODO_SPEC]
            # Clients can cache discovery before Agent binding. Offer the static schemas;
            # dispatch still checks the live pane's catalog, scope and turn grant.
            specs += list(APP_SPECS) + list(ACTIVITY_SPECS) + list(MEDIA_SPECS)
            from relay_core.keybindings import KeybindingCatalog
            specs.append(KeybindingCatalog.tool_spec())
        # Guest clients cache discovery before a turn has remote context. Keep the remote
        # schemas stable, but require an explicit host and validate capabilities on every call.
        from relay_core.tools import TOOLS, JOB_TOOLS, with_host
        from relay_core.terminal_handoff import SPEC as TERMINAL_SPEC
        from relay_core.program_input import SPEC as PROGRAM_SPEC
        remote = [copy.deepcopy(with_host(s)) for s in TOOLS
                  if s['function']['name'] in REMOTE_ALLOW]
        for spec in remote:
            f = spec['function']
            f['description'] = ("Operate on the active Relay SSH host over its existing connection. "
                                "An explicit host is required; this tool never runs locally. " + f['description'])
            f['parameters']['required'] = [*f['parameters']['required'], 'host']
            f['parameters']['properties']['host']['description'] = (
                "Required: the active SSH host named by Relay context. Never omit or guess it.")
        specs += remote + [TERMINAL_SPEC, PROGRAM_SPEC] + [s for s in JOB_TOOLS
                    if s['function']['name'] in EXEC_ALLOW]
        from relay_core.terminal_context import TOOL_SPECS as CONTEXT_SPECS
        specs += CONTEXT_SPECS
        from relay_core.planning import WRITE_PLAN_SPEC, EXIT_PLAN_MODE_SPEC
        specs += [WRITE_PLAN_SPEC, EXIT_PLAN_MODE_SPEC]
        # Protocol 36 (#C0Q8): the tool group of the pane's task-plugin workspace, offered while
        # that workspace has the plugin active — the same object gates the native agent's list.
        plugin_tools = getattr(self.agent, 'plugin_tools', None) if self.agent is not None else None
        plugin_specs = plugin_tools.specs() if plugin_tools is not None else []
        # Card #SSRQ: the user's MCP servers ride this bridge rather than the guest's own MCP
        # config, so an untrusted server's call draws Relay's approval ask like a native pane's.
        mcp_tools = getattr(self.agent, 'mcp_tools', None) if self.agent is not None else None
        if mcp_tools is not None:
            plugin_specs = plugin_specs + mcp_tools.specs()
        allowed = ALLOW | {spec['function']['name'] for spec in plugin_specs}
        specs = copy.deepcopy(specs + plugin_specs)
        return [{'name': f['name'], 'description': f.get('description', ''),
                 'inputSchema': f['parameters']}
                for spec in specs for f in [spec['function']] if f['name'] in allowed]

    def tools_version(self):
        """A digest of the names offered now; it changes when the pane's workspace does."""
        import hashlib
        names = sorted(spec['name'] for spec in self.specs())
        return hashlib.sha256('\n'.join(names).encode()).hexdigest()[:16]

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
        if request.get('method') == 'tools/version':
            # Read-only and outside the lock, so a long cell running in the kernel does not hold
            # up the proxy's poll.
            return {'version': self.tools_version()} if not self.closed else failure('unavailable', 'Board bridge is closed.')
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
            terminal_read = params.get("name") in TERMINAL_CONTEXT_ALLOW
            if key in self.cache and not terminal_read:
                old, result = self.cache[key]
                return result if old == payload else failure('request_reused', 'Request id reused with different arguments.')
            if len(self.cache) >= 10000:
                return failure('request_limit', 'Restart the guest to make more board calls.')
            def remember(result):
                if not terminal_read:
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
                if name in REMOTE_ALLOW and not args.get('host'):
                    raise ValueError('An explicit active SSH host is required; nothing ran locally.')
                # Return a job before the MCP transport's deadline, including slow commands.
                if name == 'run_command':
                    wait = args.get('timeout_seconds', WAIT_SECONDS)
                    if isinstance(wait, bool) or not isinstance(wait, int) or wait < 1:
                        raise ValueError('timeout_seconds must be a positive integer.')
                    args['timeout_seconds'] = min(wait, WAIT_SECONDS)
                elif name == 'command_output':
                    wait = args.get('wait_seconds', 0)
                    if isinstance(wait, bool) or not isinstance(wait, int) or wait < 0:
                        raise ValueError('wait_seconds must be a nonnegative integer.')
                    args['wait_seconds'] = min(wait, WAIT_SECONDS)
                # Resolve the deferred group, then use precisely the native policy path.
                from relay_core import tool_groups
                group = tool_groups.group_of(name)
                plugin_tools = getattr(self.agent, 'plugin_tools', None)
                if not group and plugin_tools is not None:
                    group = plugin_tools.group_of(name)
                mcp_tools = getattr(self.agent, 'mcp_tools', None)
                if not group and mcp_tools is not None:
                    group = mcp_tools.group_of(name)
                if group:
                    self.agent.loaded_tool_groups.add(group)
                # Claude reports its actual model in first-turn init, after Agent.ask
                # initially signs the board. Refresh from the live worker configuration.
                self.agent.sign_board()
                remote_before = copy.deepcopy(self.agent.executor.remote_session) if name in REMOTE_ALLOW else None
                prepared = self.agent._prepare(name, args)
                if active[1].is_set():
                    raise ValueError('Tool was cancelled before execution.')
                if name in REMOTE_ALLOW and remote_before != self.agent.executor.remote_session:
                    raise ValueError('The SSH session changed while preparing this tool; nothing was executed. Try again against the current host.')
                if name == 'tests_run':
                    result = self._run_named_tests(prepared, active)
                elif name == 'agent_wait' or (name == 'agent' and not prepared.arguments.get('background')):
                    result = self._run_delegation(prepared, active)
                else:
                    result = self.agent._execute(prepared, getattr(self.agent, '_turn', None))
                # #MEMS: the pane draws Keep / Edit / No from a Relay agent's own `suggest` call,
                # but a guest's reaches it under the guest's tool name with its result trimmed to
                # a count, so the outcome is told to the pane directly.
                if (name == 'app_user_memory' and args.get('action') == 'suggest' and isinstance(result, dict)
                        and result.get('status') in ('pending', 'declined', 'duplicate')):
                    self.agent.emit({'event': 'memory_suggested', 'result': {
                        key: result.get(key) for key in ('status', 'id', 'name', 'fact', 'source', 'matched')}})
            except Exception as exc:
                # Cache even an ambiguous dispatch failure: no automatic write replay.
                from .tool_outcomes import exception_code
                result = failure('board_call_failed', str(exc))
                code = exception_code(exc)
                if code:
                    result['error_code'] = code
            return remember(result)

    def _run_named_tests(self, prepared, active):
        """Let Stop revoke a long native test run while the MCP call waits for its table.

        The normal bridge lock serializes writes and teardown. A named test run can last
        30 minutes, so its native runner runs on a thread while we release that lock.
        Every other call still passes through BoardTools' own one-run gate.
        """
        done = threading.Event()
        outcome = {}
        agent = self.agent

        def run():
            try:
                outcome['result'] = agent._execute(prepared, getattr(agent, '_turn', None))
            except Exception as exc:
                outcome['error'] = exc
            finally:
                done.set()

        threading.Thread(target=run, name='relay-guest-named-tests', daemon=True).start()
        self.lock.release()
        try:
            while not done.wait(.1):
                if active[1].is_set() or active is not self.active:
                    try:
                        agent.board._tests().stop_run()
                    except Exception:
                        # The runner may still be starting, or may have just finished.
                        pass
            if active[1].is_set() or active is not self.active:
                raise ValueError('The guest turn stopped while its named tests were running.')
        finally:
            self.lock.acquire()
        if 'error' in outcome:
            raise outcome['error']
        return outcome['result']

    def _run_delegation(self, prepared, active):
        """Wait through Relay's manager without blocking Stop or another bridge call."""
        agent = self.agent
        self.lock.release()
        try:
            result = agent._execute(prepared, getattr(agent, '_turn', None))
        finally:
            self.lock.acquire()
        if active[1].is_set() or active is not self.active:
            raise ValueError('The guest turn stopped while waiting for its subagent.')
        return result

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
        sock.settimeout(LONG_CALL_SECONDS if method == 'tools/call' and isinstance(params, dict)
                        and (params.get('name') in ('tests_run', 'agent', 'agent_wait', *PLUGIN_LONG_TOOLS)
                             # An MCP call can wait on its server and on an approval ask (#SSRQ);
                             # its own deadline lives in mcp_client, and Stop cancels it.
                             or str(params.get('name') or '').startswith('mcp_')) else 30)
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

    stop = threading.Event()
    watching = []

    def watch_tools():
        """Tell the guest when the pane's tools change (protocol 36): it re-lists them."""
        last = None
        while not stop.wait(LIST_POLL_SECONDS):
            try:
                version = exchange(capability, 'tools/version').get('version')
            except (ValueError, TypeError, OSError):
                continue
            if last is not None and version and version != last:
                write({'jsonrpc': '2.0', 'method': 'notifications/tools/list_changed'})
            last = version or last

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
            stop.set()
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
                          'capabilities': {'tools': {'listChanged': True}},
                          'serverInfo': {'name': 'relay_board', 'version': '1'}}
                if not watching:
                    watching.append(threading.Thread(target=watch_tools, name='relay-board-tools-watch',
                                                     daemon=True))
                    watching[0].start()
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
