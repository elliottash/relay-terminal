"""Protocol section 11: routing assist, thinking events, turn records, skill refine/import.
Fake providers and local git repositories only (no network)."""
import io
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

from relay_core import route_assist, router, skill_manage, skills
from relay_core.agent import MAX_TURN_LOG, Agent
from relay_core.observe_protocol import ObserveCommands
from relay_core.provider import ChatProvider, ProviderConfig, ProviderError
from relay_core.queue import TurnSupervisor
from relay_core.router import classify

sys.path.insert(0, str(Path(__file__).parent))
from test_queue import Recorder  # noqa: E402
from test_sessions import ScriptedProvider, call, text, tools_msg  # noqa: E402

CONFIG = ProviderConfig('http://127.0.0.1:12345/v1', 'mock', '')


def make_bin(directory: Path, *names):
    directory.mkdir(parents=True, exist_ok=True)
    for name in names:
        path = directory / name
        path.write_text('#!/bin/sh\nexit 0\n')
        path.chmod(0o755)


class RoutingAssistClassifierTests(unittest.TestCase):
    """The owner's examples. go/install/make/find are fake executables so the test does not depend on
    what is installed; everything else on PATH is still available."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        make_bin(root / 'bin', 'go', 'install', 'make', 'find', 'open', 'kill', 'sort', 'echo', 'watch', 'date')
        self.cwd = root / 'ws'
        self.cwd.mkdir()
        (self.cwd / 'README').write_text('x')
        self.path = f"{root / 'bin'}:{os.environ['PATH']}"

    def route(self, text, **kw):
        return classify(text, path=self.path, cwd=str(self.cwd), **kw)

    def assertShell(self, text, **kw):
        d = self.route(text, **kw)
        self.assertEqual((d.route, d.needs_assist), ('shell', False), f'{text!r}: {d.reason}')
        return d

    def assertAssist(self, text, guess='agent'):
        d = self.route(text)
        self.assertTrue(d.needs_assist, f'{text!r}: {d.reason}')
        self.assertEqual(d.route, guess, text)
        self.assertTrue(d.valid)
        self.assertTrue(d.assist_reason)
        return d

    def test_owner_examples(self):
        self.assertShell('go build ./...')
        self.assertShell('go test ./...')
        self.assertAssist('go to the docs folder and summarize')
        self.assertShell('make')
        self.assertShell('make test')
        self.assertAssist('make the tests pass')
        self.assertAssist('install ripgrep')
        self.assertShell('time make')
        self.assertShell('find . -name x')
        self.assertAssist('find where the config is loaded')

    def test_more_cases(self):
        self.assertAssist('go ahead')
        self.assertAssist('kill it')
        self.assertAssist('sort the imports in main.py')
        self.assertAssist('open settings')
        self.assertShell('open README')          # a file with that name exists here
        self.assertShell('watch date')
        self.assertShell('date')
        self.assertShell('install -m 644 a b')
        self.assertShell('echo hello')
        # Literal-text commands: assist, but the local guess stays shell.
        self.assertAssist('echo hello to you all', guess='shell')
        # Quotes, operators and flags are shell-typical.
        self.assertShell("echo 'why does this fail?'")
        self.assertShell('find . -name the | sort')

    def test_unchanged_behaviour(self):
        self.assertEqual(self.route('frobnicate the widget').route, 'agent')
        self.assertFalse(self.route('frobnicate the widget').needs_assist)   # invalid: agent as before
        d = self.route('why is the build failing?')
        self.assertEqual((d.route, d.needs_assist), ('agent', False))       # "why" is not a command
        self.assertEqual(self.route('install ripgrep', mode='shell').route, 'shell')
        self.assertFalse(self.route('install ripgrep', mode='shell').needs_assist)
        self.assertFalse(self.route('/agent go to the docs').needs_assist)
        self.assertIn('needs_assist', self.route('ls').to_dict())
        self.assertIn('assist_reason', self.route('ls').to_dict())

    def test_english_list_is_inclusive(self):
        for word in ('go install make find open test build start stop run time date help man watch sort head tail '
                     'cut join split which who yes true false sleep kill top less more file fold look write wall last '
                     'link touch mount echo print printf read apply dig host ping see say tree clear reset history '
                     'source alias export set env').split():
            self.assertIn(word, router.ENGLISH_COMMANDS)


class ExecutableScanTests(unittest.TestCase):
    def test_new_install_appears_and_scan_is_cached(self):
        with tempfile.TemporaryDirectory() as temp:
            bin_dir = Path(temp) / 'bin'
            make_bin(bin_dir, 'alpha')
            path = str(bin_dir)
            self.assertIn('alpha', router.path_executables(path))
            with mock.patch('relay_core.router.os.scandir', side_effect=AssertionError('rescanned')):
                self.assertIn('alpha', router.path_executables(path))   # unchanged mtime: cached
            self.assertEqual(classify('beta --x', path=path, cwd=temp).route, 'agent')
            make_bin(bin_dir, 'beta')
            st = os.stat(bin_dir)
            os.utime(bin_dir, ns=(st.st_atime_ns, st.st_mtime_ns + 10_000_000))
            self.assertIn('beta', router.path_executables(path))
            self.assertEqual(classify('beta --x', path=path, cwd=temp).route, 'shell')
            # Non-executable files are not commands.
            (bin_dir / 'data.txt').write_text('x')
            self.assertNotIn('data.txt', router.path_executables(path))


class FakeSide:
    def __init__(self, reply='{"route":"agent","confidence":0.9,"reason":"English request"}', delay=0.0, error=None):
        self.reply, self.delay, self.error = reply, delay, error
        self.requests, self.cancelled = [], False

    def complete(self, messages, tools, emit, cancel):
        self.requests.append((messages, tools))
        deadline = time.monotonic() + self.delay
        while time.monotonic() < deadline:
            if cancel.is_set():
                raise ProviderError('cancelled')
            time.sleep(0.01)
        if self.error:
            raise self.error
        return {'role': 'assistant', 'content': self.reply}

    def cancel(self):
        self.cancelled = True


class RouteAssistTests(unittest.TestCase):
    def run_assist(self, provider, **request):
        rec = Recorder()
        route_assist.run(provider, {'type': 'route_assist', 'id': 'r1', 'text': 'install ripgrep', 'cwd': '/tmp',
                                    'mode': 'auto', **request}, rec)
        return rec.wait(lambda e: e['event'] == 'route_assisted'), rec

    def test_success_is_no_tools_json(self):
        provider = FakeSide()
        event, _ = self.run_assist(provider)
        self.assertEqual((event['id'], event['route'], event['confidence']), ('r1', 'agent', 0.9))
        self.assertEqual(event['reason'], 'English request')
        self.assertIsInstance(event['elapsed_ms'], int)
        messages, tools = provider.requests[0]
        self.assertEqual(tools, [])
        self.assertIn('JSON only', messages[0]['content'])
        self.assertIn('install ripgrep', messages[1]['content'])

    def test_fenced_and_clamped(self):
        event, _ = self.run_assist(FakeSide('```json\n{"route": "shell", "confidence": 7}\n```'))
        self.assertEqual((event['route'], event['confidence']), ('shell', 1.0))

    def test_errors_and_timeouts_return_null_route(self):
        event, _ = self.run_assist(FakeSide('I think maybe'))
        self.assertIsNone(event['route'])
        self.assertIn('no route', event['error'])
        event, _ = self.run_assist(FakeSide(error=ProviderError('Provider HTTP 401.')))
        self.assertEqual((event['route'], event['error']), (None, 'Provider HTTP 401.'))
        slow = FakeSide(delay=2.0)
        started = time.monotonic()
        event, rec = self.run_assist(slow, timeout_ms=150)
        self.assertLess(time.monotonic() - started, 1.0)
        self.assertEqual((event['route'], event['error']), (None, 'timeout'))
        self.assertTrue(slow.cancelled)
        time.sleep(0.3)
        self.assertEqual(len(rec.of('route_assisted')), 1)   # the late answer is dropped

    def test_default_timeout_is_two_seconds(self):
        self.assertEqual(route_assist.validate({'text': 'x'})['timeout_ms'], 2000)
        with self.assertRaises(ValueError):
            route_assist.validate({'text': ''})
        with self.assertRaises(ValueError):
            route_assist.validate({'text': 'x', 'timeout_ms': 99999})

    def test_worker_handler_uses_pane_provider_cheaply(self):
        rec = Recorder()
        turns = TurnSupervisor(rec)
        self.addCleanup(turns.shutdown)
        commands = ObserveCommands(turns, rec)
        # Never reach the real keyring or network from tests: no fast router key here.
        patcher = mock.patch.object(route_assist, 'router_provider', return_value=None)
        patcher.start(); self.addCleanup(patcher.stop)
        commands.handle('route_assist', {'id': 'x', 'text': 'go to the docs'})
        self.assertEqual(rec.wait(lambda e: e['event'] == 'route_assisted')['error'], 'not_configured')
        with tempfile.TemporaryDirectory() as ws:
            config = ProviderConfig('https://api.moonshot.ai/v1', 'kimi-k3', 'sk-test', {'reasoning_effort': 'max'}, 8192)
            agent = Agent(config, ws, rec, preset_id='kimi')
            side = agent.side_provider(cheap=True, max_tokens=route_assist.MAX_TOKENS)
            self.assertEqual(side.config.max_tokens, route_assist.MAX_TOKENS)
            self.assertEqual(side.config.extra['reasoning_effort'], 'low')
            self.assertEqual(agent.config.max_tokens, 8192)
            fake = FakeSide('{"route":"shell","confidence":0.7}')
            turns.set_agent(Agent(CONFIG, ws, turns.agent_emit, provider=fake))
            commands.handle('route_assist', {'id': 'y', 'text': 'make test'})
            event = rec.wait(lambda e: e['event'] == 'route_assisted' and e['id'] == 'y')
            self.assertEqual(event['route'], 'shell')


def sse(delta=None, finish=None):
    return ('data: ' + json.dumps({'choices': [{'delta': delta or {}, 'finish_reason': finish}]}) + '\n\n').encode()


class ThinkingStreamTests(unittest.TestCase):
    def parse(self, data, extra=None):
        provider = ChatProvider(ProviderConfig('http://127.0.0.1:1/v1', 'm', '', extra or {}))
        events = []
        return provider._stream(io.BytesIO(data), events.append, threading.Event()), events

    def test_reasoning_content_streams_then_done_before_answer(self):
        data = sse({'reasoning_content': 'Let me '}) + sse({'reasoning_content': 'think.'})
        data += sse({'content': 'Answer'}) + sse(finish='stop') + b'data: [DONE]\n\n'
        message, events = self.parse(data)
        kinds = [e['event'] for e in events if e['event'] in ('thinking_delta', 'thinking_done', 'delta')]
        self.assertEqual(kinds, ['thinking_delta', 'thinking_delta', 'thinking_done', 'delta'])
        self.assertEqual([e['text'] for e in events if e['event'] == 'delta'], ['Answer'])
        done = [e for e in events if e['event'] == 'thinking_done'][0]
        self.assertEqual(done['chars'], len('Let me think.'))
        self.assertIsInstance(done['elapsed_ms'], int)
        self.assertEqual(message['reasoning_content'], 'Let me think.')   # still preserved for the next request

    def test_openrouter_reasoning_and_details_not_duplicated(self):
        both = {'reasoning': 'abc', 'reasoning_details': [{'type': 'reasoning.text', 'text': 'abc'}]}
        only_details = {'reasoning_details': [{'type': 'reasoning.summary', 'summary': 'sum'},
                                              {'type': 'reasoning.encrypted', 'data': 'xxx'}]}
        data = sse(both) + sse(only_details) + sse({'tool_calls': [
            {'index': 0, 'id': 'c', 'type': 'function', 'function': {'name': 'read_file', 'arguments': '{}'}}]})
        data += sse(finish='tool_calls') + b'data: [DONE]\n\n'
        message, events = self.parse(data)
        self.assertEqual([e['text'] for e in events if e['event'] == 'thinking_delta'], ['abc', 'sum'])
        self.assertEqual(len([e for e in events if e['event'] == 'thinking_done']), 1)
        self.assertEqual(message['reasoning'], 'abc')

    def test_thinking_done_at_stream_end_without_answer(self):
        _, events = self.parse(sse({'reasoning_content': 'hmm'}) + sse(finish='stop') + b'data: [DONE]\n\n')
        self.assertEqual(events[-1]['event'], 'thinking_done')

    def test_elapsed_counts_from_request_start(self):
        # GLM buffers reasoning and sends it in one burst; the time before the burst is thinking time too.
        provider = ChatProvider(ProviderConfig('http://127.0.0.1:1/v1', 'm', ''))
        events = []
        data = sse({'reasoning_content': 'x'}) + sse({'content': 'y'}) + sse(finish='stop') + b'data: [DONE]\n\n'
        provider._stream(io.BytesIO(data), events.append, threading.Event(), time.monotonic() - 1.5)
        self.assertGreaterEqual([e for e in events if e['event'] == 'thinking_done'][0]['elapsed_ms'], 1500)


class ThinkingProvider:
    """Streams reasoning through emit like ChatProvider, then answers or calls a tool."""

    def __init__(self, responses):
        self.responses = list(responses)

    def complete(self, messages, tools, emit, cancel):
        response = self.responses.pop(0)
        if response.get('reasoning_content'):
            emit({'event': 'thinking_delta', 'text': response['reasoning_content']})
            emit({'event': 'thinking_done', 'elapsed_ms': 7, 'chars': len(response['reasoning_content'])})
        if response.get('content'):
            emit({'event': 'delta', 'text': response['content']})
        return response

    def cancel(self):
        pass


class TurnRecordTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.ws = Path(self.temp.name)

    def test_turn_ids_summary_outputs_and_transcript(self):
        (self.ws / 'hello.txt').write_text('hi\n')
        provider = ThinkingProvider([
            {**tools_msg(call('run_command', {'command': 'cat hello.txt'}, 'c1'),
                         call('run_command', {'command': 'exit 3'}, 'c2')), 'reasoning_content': 'plan it'},
            {**text('All done.'), 'reasoning_content': 'wrap up'}])
        events = []
        agent = Agent(CONFIG, str(self.ws), events.append, provider=provider)
        agent.ask('look at hello', turn_id='T1')
        for kind in ('tool_started', 'tool_result', 'done', 'thinking_delta', 'thinking_done'):
            tagged = [e for e in events if e['event'] == kind]
            self.assertTrue(tagged, kind)
            self.assertTrue(all(e['turn_id'] == 'T1' for e in tagged), kind)
        self.assertEqual([e['call_id'] for e in events if e['event'] == 'tool_started'], ['c1', 'c2'])
        self.assertEqual(events[-1]['event'], 'done')
        summary = events[-2]
        self.assertEqual(summary['event'], 'turn_summary')
        self.assertEqual((summary['turn_id'], summary['outcome'], summary['thinking_ms']), ('T1', 'done', 14))
        self.assertEqual([(t['call_id'], t['name'], t['ok'], t.get('exit_code')) for t in summary['tools']],
                         [('c1', 'run_command', True, 0), ('c2', 'run_command', False, 3)])
        self.assertEqual(summary['tools'][0]['preview'], 'cat hello.txt')
        output = agent.tool_output('T1', 'c1')
        self.assertEqual((output['event'], output['name'], output['stored']), ('tool_output', 'run_command', True))
        self.assertIn('hi', output['result']['output'])
        self.assertIn('cat hello.txt', output['preview'])
        transcript = agent.turn_transcript('T1')
        self.assertEqual([i['role'] for i in transcript['items']], ['user', 'assistant', 'tool', 'tool', 'assistant'])
        self.assertEqual(transcript['items'][1]['tool_calls'], ['run_command', 'run_command'])
        self.assertEqual(transcript['items'][2]['tool_call_id'], 'c1')
        self.assertFalse(transcript['running'])
        with self.assertRaises(ValueError):
            agent.tool_output('T1', 'nope')
        with self.assertRaises(ValueError):
            agent.turn_transcript('missing')

    def test_generated_ids_and_last_50_kept(self):
        provider = ScriptedProvider()
        events = []
        agent = Agent(CONFIG, str(self.ws), events.append, provider=provider)
        for i in range(MAX_TURN_LOG + 3):
            agent.ask(f'prompt {i}')
        ids = [e['turn_id'] for e in events if e['event'] == 'done']
        self.assertEqual(len(set(ids)), MAX_TURN_LOG + 3)
        self.assertEqual(len(agent.turn_log), MAX_TURN_LOG)
        with self.assertRaises(ValueError):
            agent.turn_transcript(ids[0])
        self.assertEqual(agent.turn_transcript(ids[-1])['items'][0]['content'], f'prompt {MAX_TURN_LOG + 2}')

    def test_failed_turn_still_summarised_and_transcript_kept(self):
        class Failing:
            def complete(self, messages, tools, emit, cancel):
                emit({'event': 'thinking_delta', 'text': 'partial'})
                raise ProviderError('Provider HTTP 500.')

            def cancel(self):
                pass
        events = []
        agent = Agent(CONFIG, str(self.ws), events.append, provider=Failing())
        agent.ask('x', turn_id='T9')
        kinds = [e['event'] for e in events if e['event'] in ('thinking_delta', 'thinking_done', 'turn_summary', 'error')]
        self.assertEqual(kinds, ['thinking_delta', 'thinking_done', 'turn_summary', 'error'])
        self.assertEqual(events[-1]['turn_id'], 'T9')
        self.assertEqual(agent.turn_transcript('T9')['items'][0]['content'], 'x')
        self.assertEqual(agent.turn_transcript('T9')['outcome'], 'error')

    def test_queue_turn_id_is_item_id_and_protocol_handlers(self):
        rec = Recorder()
        turns = TurnSupervisor(rec)
        self.addCleanup(turns.shutdown)
        provider = ScriptedProvider([tools_msg(call('list_directory', {'path': '.'}, 'c7')), text('ok')])
        turns.set_agent(Agent(CONFIG, str(self.ws), turns.agent_emit, provider=provider))
        commands = ObserveCommands(turns, rec)
        item = turns.submit('list it')
        started = rec.wait(lambda e: e['event'] == 'agent_started')
        self.assertEqual(started['turn_id'], item)
        done = rec.wait(lambda e: e['event'] == 'done')
        self.assertEqual(done['turn_id'], item)
        rec.wait(lambda e: e['event'] == 'agent_finished')
        commands.handle('tool_output_get', {'id': 'q', 'turn_id': item, 'call_id': 'c7'})
        output = rec.wait(lambda e: e['event'] == 'tool_output' and e.get('id') == 'q')
        self.assertEqual(output['name'], 'list_directory')
        commands.handle('turn_transcript_get', {'id': 'r', 'turn_id': item})
        transcript = rec.wait(lambda e: e['event'] == 'turn_transcript')
        self.assertEqual(transcript['items'][0], {'role': 'user', 'content': 'list it'})


SKILL = '---\nname: {name}\ndescription: {description}\nlicense: MIT\nallowed-tools:\n  - Bash\n---\n{body}\n'


def write(path: Path, content: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding='utf-8')


class SkillsHome(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.home = self.root / 'home'
        self.ws = self.root / 'ws'
        self.ws.mkdir(parents=True)
        env = {'HOME': str(self.home), 'XDG_CONFIG_HOME': str(self.home / '.config'),
               'XDG_DATA_HOME': str(self.home / '.local/share')}
        patcher = mock.patch.dict(os.environ, env)
        patcher.start()
        self.addCleanup(patcher.stop)


class RefineTests(SkillsHome):
    def test_refine_writes_copy_preserving_name_and_wins_over_original(self):
        original_dir = self.home / '.warp/skills/deploy'
        original = SKILL.format(name='deploy', description='Deploy the site. Use when asked to ship.',
                                body='# Deploy\nrun scripts/go.sh then check it')
        write(original_dir / 'SKILL.md', original)
        write(original_dir / 'scripts/go.sh', 'echo go\n')
        reply = ('```markdown\n---\nname: something-else\ndescription: "Deploys the site; use when the user asks to ship."\n'
                 '---\n# Deploy\n\n1. Run `scripts/go.sh`.\n2. Check the result.\n```')
        rec = Recorder()
        turns = TurnSupervisor(rec)
        self.addCleanup(turns.shutdown)
        index = skills.from_request(None, str(self.ws))
        agent = Agent(CONFIG, str(self.ws), turns.agent_emit, provider=ScriptedProvider(side_reply=reply), skills=index)
        turns.set_agent(agent)
        commands = ObserveCommands(turns, rec)
        commands.handle('refine_skills', {'id': 'f', 'names': ['deploy', 'missing']})
        event = rec.wait(lambda e: e['event'] == 'skills_refined')
        self.assertEqual(event['errors'][0]['name'], 'missing')
        item = event['items'][0]
        target = self.home / '.config/relay/skills/deploy/SKILL.md'
        self.assertEqual((item['name'], item['path'], item['from']), ('deploy', str(target), str(original_dir / 'SKILL.md')))
        refined = target.read_text()
        fields = skills.parse_frontmatter(refined)
        self.assertEqual(fields['name'], 'deploy')
        self.assertEqual(fields['description'], 'Deploys the site; use when the user asks to ship.')
        self.assertEqual(fields['refined_from'], str(original_dir / 'SKILL.md'))
        self.assertIn('allowed-tools:\n  - Bash', refined)
        self.assertIn('1. Run `scripts/go.sh`.', refined)
        self.assertNotIn('```', refined)
        self.assertEqual((target.parent / 'scripts/go.sh').read_text(), 'echo go\n')
        self.assertEqual((original_dir / 'SKILL.md').read_text(), original)     # original untouched
        # The refined copy wins; the clash is reported; the running agent picked it up.
        self.assertTrue(event['reloaded'])
        self.assertEqual(agent.executor.skills.skills['deploy'].root, target.parent.resolve())
        self.assertTrue(any('refined copy' in s for s in agent.executor.skills.skipped))
        commands.handle('skills_list', {'id': 'l'})
        listing = rec.wait(lambda e: e['event'] == 'skills')['items']
        deploy = [i for i in listing if i['name'] == 'deploy']
        self.assertEqual([i['source'] for i in deploy], ['relay-refined', 'warp'])
        self.assertEqual(deploy[0]['refined_from'], str(original_dir / 'SKILL.md'))
        self.assertEqual(deploy[1]['shadowed_by'], str(target))
        self.assertFalse(deploy[0]['excluded'])
        # Refining the refined copy again keeps pointing at the original.
        commands.handle('refine_skills', {'id': 'g', 'names': ['deploy']})
        again = rec.wait(lambda e: e['event'] == 'skills_refined' and e.get('id') == 'g')
        self.assertEqual(again['items'][0]['from'], str(original_dir / 'SKILL.md'))

    def test_bad_model_reply_is_rejected(self):
        write(self.home / '.warp/skills/a/SKILL.md', SKILL.format(name='a', description='d', body='body text here'))
        listing = skill_manage.list_skills(skills.default_directories(str(self.ws)))
        with self.assertRaises(skills.SkillError):
            skill_manage.refine_one(ScriptedProvider(side_reply='Sure! Here you go.'), listing, 'a', self.root / 'out')
        self.assertFalse((self.root / 'out/a').exists())

    def test_excluded_skills_are_listed(self):
        write(self.home / '.warp/skills/warpctrl/SKILL.md', SKILL.format(name='warpctrl', description='d', body='b'))
        items = skill_manage.list_skills(*skill_manage.index_settings(None, str(self.ws)))
        self.assertEqual([(i['name'], i['excluded']) for i in items], [('warpctrl', True)])


def git(*args, cwd):
    subprocess.run(['git', *args], cwd=cwd, check=True, capture_output=True,
                   env={**os.environ, 'GIT_AUTHOR_NAME': 't', 'GIT_AUTHOR_EMAIL': 't@t', 'GIT_COMMITTER_NAME': 't',
                        'GIT_COMMITTER_EMAIL': 't@t'})
    return subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=cwd, capture_output=True, text=True).stdout.strip()


class ImportTests(SkillsHome):
    def setUp(self):
        super().setUp()
        patcher = mock.patch.object(skill_manage, 'ALLOW_FILE_URLS', True)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.repo = self.root / 'skills-repo'
        write(self.repo / 'skills/lint/SKILL.md', SKILL.format(name='lint', description='Lint code', body='run the linter'))
        write(self.repo / 'skills/lint/ref/rules.md', 'rules\n')
        write(self.repo / 'skills/docs/SKILL.md', SKILL.format(name='docs', description='Write docs', body='write docs'))
        write(self.repo / 'other/nodesc/SKILL.md', '---\nname: nodesc\n---\nbody\n')
        (self.repo / 'skills/evil').mkdir(parents=True)
        os.symlink('/etc/passwd', self.repo / 'skills/evil/SKILL.md')
        os.symlink('/etc', self.repo / 'skills/lint/etc-link')
        git('init', '-q', '-b', 'main', cwd=self.repo)
        git('add', '-A', cwd=self.repo)
        self.first = git('commit', '-q', '-m', 'one', cwd=self.repo)
        self.url = self.repo.as_uri()

    def test_preview_confirm_and_updates(self):
        rec = Recorder()
        turns = TurnSupervisor(rec)
        self.addCleanup(turns.shutdown)
        commands = ObserveCommands(turns, rec)
        self.addCleanup(commands.shutdown)
        commands.handle('import_skills_preview', {'id': 'p', 'url': self.url})
        preview = rec.wait(lambda e: e['event'] in ('skills_import_preview', 'error'))
        self.assertEqual(preview['event'], 'skills_import_preview', preview)
        self.assertEqual(preview['commit'], self.first)
        self.assertEqual(sorted(i['name'] for i in preview['items']), ['docs', 'lint'])
        lint = [i for i in preview['items'] if i['name'] == 'lint'][0]
        self.assertEqual(lint['path'], 'skills/lint')
        self.assertIn('ref/rules.md', lint['files'])
        self.assertTrue(any('evil' in s for s in preview['skipped']))
        self.assertTrue(any('nodesc' in s for s in preview['skipped']))

        # A new upstream commit does not change what was previewed and pinned.
        write(self.repo / 'skills/lint/SKILL.md', SKILL.format(name='lint', description='Lint v2', body='new'))
        git('commit', '-q', '-am', 'two', cwd=self.repo)
        commands.handle('import_skills_confirm', {'id': 'c', 'url': self.url, 'commit': self.first, 'names': ['lint']})
        imported = rec.wait(lambda e: e['event'] in ('skills_imported', 'error') and e.get('id') == 'c')
        self.assertEqual(imported['event'], 'skills_imported', imported)
        destination = self.home / f'.local/share/relay/skill-imports/skills-repo@{self.first}'
        self.assertEqual(imported['dir'], str(destination))
        self.assertIn('Lint code', (destination / 'lint/SKILL.md').read_text())
        self.assertEqual((destination / 'lint/ref/rules.md').read_text(), 'rules\n')
        # core.symlinks=false: a repository symlink is checked out as a small text file, never a link.
        self.assertFalse((destination / 'lint/etc-link').is_symlink())
        self.assertFalse((destination / 'docs').exists())
        # Imported skills are in the default search path.
        index = skills.from_request(None, str(self.ws))
        self.assertIn('lint', index.skills)
        commands.handle('skills_list', {'id': 'l'})
        listing = rec.wait(lambda e: e['event'] == 'skills')['items']
        self.assertEqual([i['source'] for i in listing if i['name'] == 'lint'], [f'import:skills-repo@{self.first}'])

        commands.handle('skills_check_updates', {'id': 'u', 'url': self.url, 'ref': 'main'})
        updates = rec.wait(lambda e: e['event'] in ('skills_updates', 'error') and e.get('id') == 'u')
        self.assertEqual(updates['event'], 'skills_updates', updates)
        self.assertEqual(updates['current'], self.first)
        self.assertNotEqual(updates['latest'], self.first)
        self.assertTrue(updates['update_available'])

    def test_confirm_refetches_pinned_commit_and_rejects_unknown_names(self):
        imports = skill_manage.SkillImports()
        self.addCleanup(imports.shutdown)
        event = imports.confirm(self.url, self.first, ['docs'])
        self.assertEqual(event['items'][0]['name'], 'docs')
        with self.assertRaises(ValueError):
            imports.confirm(self.url, self.first, ['evil'])
        with self.assertRaises(ValueError):
            imports.confirm(self.url, 'abc', ['docs'])

    def test_ref_preview(self):
        git('checkout', '-q', '-b', 'feature', cwd=self.repo)
        write(self.repo / 'skills/extra/SKILL.md', SKILL.format(name='extra', description='Extra', body='x'))
        git('add', '-A', cwd=self.repo)
        feature = git('commit', '-q', '-m', 'feature', cwd=self.repo)
        imports = skill_manage.SkillImports()
        self.addCleanup(imports.shutdown)
        preview = imports.preview(self.url, 'feature')
        self.assertEqual(preview['commit'], feature)
        self.assertIn('extra', [i['name'] for i in preview['items']])

    def test_url_and_ref_validation(self):
        with mock.patch.object(skill_manage, 'ALLOW_FILE_URLS', False):
            for bad in ['file:///tmp/x', 'http://example.com/r.git', 'ext::sh -c touch% /tmp/pwned', '/tmp/repo',
                        '--upload-pack=touch /tmp/x', 'https://user:pw@github.com/a/b', 'fd::3', '']:
                with self.assertRaises(ValueError, msg=bad):
                    skill_manage.validate_url(bad)
            for good in ['https://github.com/anthropics/skills', 'ssh://git@github.com/a/b.git',
                         'git@github.com:anthropics/skills.git']:
                self.assertEqual(skill_manage.validate_url(good), good)
        for bad in ['--upload-pack=x', '../x', 'a b', 'x.lock']:
            with self.assertRaises(ValueError):
                skill_manage.validate_ref(bad)
        self.assertEqual(skill_manage.repo_name('git@github.com:anthropics/skills.git'), 'skills')

    def test_find_skills_refuses_escaping_paths(self):
        clone = self.root / 'clone'
        write(clone / 'ok/SKILL.md', SKILL.format(name='ok', description='fine', body='b'))
        write(self.root / 'outside/stolen/SKILL.md', SKILL.format(name='stolen', description='x', body='b'))
        os.symlink(self.root / 'outside', clone / 'linked')
        items, skipped = skill_manage.find_skills(clone)
        self.assertEqual([i['name'] for i in items], ['ok'])
        self.assertTrue(any('linked' in s for s in skipped))


if __name__ == '__main__':
    unittest.main()


class RouterProviderTests(unittest.TestCase):
    def test_fast_router_model_used_when_openrouter_key_exists(self):
        provider = route_assist.router_provider(lookup=lambda preset: "sk-or-test" if preset == "openrouter" else "")
        self.assertIsNotNone(provider)
        self.assertEqual(provider.config.model, route_assist.ROUTER_MODEL)
        self.assertEqual(provider.config.base_url, route_assist.ROUTER_BASE_URL)
        self.assertEqual(provider.config.max_tokens, route_assist.MAX_TOKENS)

    def test_no_fast_router_without_key(self):
        self.assertIsNone(route_assist.router_provider(lookup=lambda preset: ""))
