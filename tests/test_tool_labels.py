"""The concise tool-call line (card #TK9C, protocol 23): relay_core/tool_labels.py."""
import unittest

from relay_core import tool_labels as T
from relay_core.tools import MAX_FILE, MAX_OUTPUT

MINUS = "\u2212"


class CommandClassifierTests(unittest.TestCase):
    """The heart of it: what "ran …" says for a shell command."""

    SHORT = [
        # A single line of at most 40 characters is shown exactly as it was written.
        ('ls', 'ls'),
        ('ls -la src', 'ls -la src'),
        ('git status', 'git status'),
        ('npm run dev', 'npm run dev'),
        ('pytest -q', 'pytest -q'),
        ('cd build && make', 'cd build && make'),
        ('grep -rn foo . | wc -l', 'grep -rn foo . | wc -l'),
        ('./scripts/test.sh', './scripts/test.sh'),
        ('echo "hello world"', 'echo "hello world"'),
        ('cat a.txt b.txt c.txt d.txt e.txt f.txt', 'cat a.txt b.txt c.txt d.txt e.txt f.txt'),
    ]

    LONG = [
        # Longer than 40 characters: the program's name, and the subcommand of a multi-command CLI.
        ('git commit -m "a rather long commit message here"', 'git commit'),
        ('git -C /var/tmp/checkout commit -m "long message goes here"', 'git commit'),
        ('git log --oneline --graph --decorate --all -n 40', 'git log'),
        ('npm install --save-dev typescript eslint prettier vitest', 'npm install'),
        ('pnpm --filter @relay/web run build --mode production', 'pnpm run build'),
        ('yarn workspace web add react react-dom @types/react', 'yarn workspace web'),
        ('cargo build --release --features "one two three four"', 'cargo build'),
        ('docker run --rm -it -v "$PWD":/w -w /w node:20 bash', 'docker run'),
        ('kubectl get pods --all-namespaces -o wide --sort-by .metadata.name', 'kubectl get pods'),
        ('go test ./... -run TestSomethingRatherLongIndeed -v', 'go test'),
        ('pip install -r requirements-dev.txt --upgrade --quiet', 'pip install'),
        ('apt-get install -y build-essential libqt6-dev cmake ninja', 'apt-get install'),
        ('systemctl restart some-really-long-service-name.service', 'systemctl restart'),
        ('gh pr create --title "Concise tool lines" --body-file body.md', 'gh pr create'),
        ('gh auth login --with-token < a-rather-long-token-file.txt', 'gh auth login'),
        ('make -j8 test-everything-including-the-slow-suites', 'make'),
        ('make', 'make'),
        ('ctest --test-dir build --output-on-failure -j 8 --repeat until-pass:2', 'ctest'),
        ('rsync -avz --delete /home/e/src/relay/ backup:/srv/relay/', 'rsync'),
        ('grep -rn "tool_started" --include=*.py backend/ | head -40 | sort', 'grep +2'),
        ('find . -name "*.py" -newer setup.cfg -print0 | xargs -0 wc -l', 'find +1'),
        ('cat very_long_file_name_here.txt | sort | uniq -c | sort -rn | head', 'cat +4'),
        ('./scripts/relay-board.py check --strict --and-another-flag', 'relay-board.py'),
        ('/usr/bin/python3 /opt/relay/scripts/migrate_everything.py --dry-run', 'python3 migrate_everything.py'),
        ('python3 -m pytest tests/test_agent.py -k "labels and events"', 'python3 -m pytest'),
        ('python scripts/a_very_long_script_name_that_keeps_going_on.py --x', 'python'),
    ]

    WRAPPERS = [
        ('cd backend && python3 -m unittest discover -s ../tests -v', 'python3 -m unittest'),
        ('cd /home/elliott/repos/relay-terminal; ./scripts/test.sh --verbose', 'test.sh'),
        ('FOO=1 BAR=2 pytest tests/test_agent.py -k something_rather_long', 'pytest'),
        ('env FOO=1 python manage.py migrate --noinput --settings=prod.set', 'python manage.py'),
        ('sudo apt-get install -y build-essential libqt6-dev cmake ninja', 'apt-get install'),
        ('sudo -u postgres psql -c "select 1 from a_very_long_table_name"', 'psql'),
        ('time ./scripts/build-everything.sh --release --with-tests --now', 'build-everything.sh'),
        ('timeout 30 ./scripts/test.sh --verbose --and-more-flags-here-too', 'test.sh'),
        ('timeout -k 5 300 ctest --test-dir build --output-on-failure -j8', 'ctest'),
        ('nice -n 19 ionice -c3 rsync -a /very/long/source/ /destination/', 'rsync'),
        ('xvfb-run -a ctest --test-dir build --output-on-failure --repeat 2', 'ctest'),
        ('xvfb-run --server-args="-screen 0 1280x1024x24" ./build/relay --x', 'relay'),
        ('flock /tmp/relay.lock ./scripts/deploy.sh --production --confirm', 'deploy.sh'),
        ('nohup ./scripts/watch-and-rebuild.sh --interval 5 --verbose &', 'watch-and-rebuild.sh'),
        ('stdbuf -oL ./build/relay --log-level debug --session scratchpad', 'relay'),
    ]

    SCRIPTS = [
        ("python3 - <<'PY'\nprint('hello')\nPY", 'python3 script'),
        ("python3 -c 'import sys; print(sys.version)'", 'python3 script'),
        ('python -c "print(1)" # a comment that makes this line long enough', 'python script'),
        ("bash -c 'for i in 1 2 3; do echo $i; done; echo done with it'", 'bash script'),
        ("sh -c 'test -f a && test -f b && echo both are present here ok'", 'sh script'),
        ("node -e 'console.log(require(\"os\").cpus().length, \"cpus here\")'", 'node script'),
        ("perl -e 'print join(\",\", map { $_ * 2 } 1 .. 10), \"\\n\";'", 'perl script'),
        ("cat <<'EOF' > /tmp/some/rather/long/path/notes.txt\nhello\nEOF", 'cat'),
        ("ruby -e 'puts (1..20).map { |n| n * n }.inject(:+).to_s * 3'", 'ruby script'),
    ]

    SECRETS = [
        # Nothing that reads like a credential is ever shown, however short the line is.
        ('API_KEY=abc123 curl https://x.y/z', 'curl'),
        ('TOKEN=t ./deploy.sh', 'deploy.sh'),
        ('curl -H "Authorization: Bearer sk-abcdef" https://api.example.com', 'curl'),
        ('mysql --password=hunter2 -e "select 1"', 'mysql'),
        ('export AWS_SECRET_ACCESS_KEY=xyz', 'export'),
    ]

    ODD = [
        # Never a raise, whatever arrives.
        ('', 'command'),
        ('   ', 'command'),
        ('\n\n', 'command'),
        (None, 'command'),
        (42, 'command'),
        ({'command': 'ls'}, 'command'),
        ("echo 'unbalanced", "echo 'unbalanced"),
        ('echo "unbalanced and long enough to go past the forty character rule', 'echo'),
        ('ünïcödé --with-a-flag', 'ünïcödé --with-a-flag'),
        ('日本語のコマンド --and-a-flag-that-makes-this-line-long-enough', '日本語のコマンド'),
        ('|||', 'command'),
        ('&& && &&', 'command'),
        ('a' * 200, 'a' * (T.SHORT_COMMAND - 1) + '…'),
        ('   git    status   ', 'git    status'),
        ('(cd build && make test) | tee /tmp/a-rather-long-log-name.log', 'make test +1'),
    ]

    def test_short_commands_are_shown_whole(self):
        for command, expected in self.SHORT:
            with self.subTest(command=command):
                self.assertEqual(T.command_label(command), expected)
                self.assertLessEqual(len(command), T.SHORT_COMMAND)

    def test_long_commands_shrink_to_the_program(self):
        for command, expected in self.LONG:
            with self.subTest(command=command):
                self.assertEqual(T.command_label(command), expected)

    def test_wrappers_and_env_are_skipped(self):
        for command, expected in self.WRAPPERS:
            with self.subTest(command=command):
                self.assertEqual(T.command_label(command), expected)

    def test_inline_scripts_are_a_script(self):
        for command, expected in self.SCRIPTS:
            with self.subTest(command=command):
                self.assertEqual(T.command_label(command), expected)

    def test_secrets_never_reach_the_line(self):
        for command, expected in self.SECRETS:
            with self.subTest(command=command):
                self.assertEqual(T.command_label(command), expected)

    def test_odd_input_never_raises(self):
        for command, expected in self.ODD:
            with self.subTest(command=repr(command)):
                self.assertEqual(T.command_label(command), expected)

    def test_a_pipeline_counts_the_rest(self):
        self.assertEqual(T.command_label('grep -rn "needle" backend/ | head -40 | wc -l'), 'grep +2')
        self.assertEqual(T.command_label('cd backend && pytest tests/ -k "a name that is long"'), 'pytest')
        self.assertEqual(
            T.command_label('cd x && ./a-long-script-name.sh --flag && ./another-one.sh --flag'),
            'a-long-script-name.sh +1')


class LabelTests(unittest.TestCase):
    def label(self, name, args, result, **kw):
        return T.result_label(name, args, result, **kw)

    def test_run_command(self):
        label = self.label('run_command', {'command': "python3 - <<'PY'\nprint(1)\nPY"},
                           {'output': 'x\n' * 14, 'exit_code': 0, 'duration_seconds': 1.23,
                            'truncated': False})
        self.assertEqual(label['kind'], 'run')
        self.assertEqual(label['running'], 'running python3 script')
        self.assertEqual(label['title'], 'ran python3 script')
        self.assertEqual(label['stats'], ['14 lines', 'exit 0', '1.2 s'])
        self.assertTrue(label['ok'])
        self.assertEqual(label['open'], {'type': 'fold'})
        self.assertNotIn('merge', label)

    def test_a_failing_command_still_ran(self):
        label = self.label('run_command', {'command': 'pytest'},
                           {'output': 'x\n' * 212, 'exit_code': 1, 'duration_seconds': 8.0})
        self.assertEqual(label['title'], 'ran pytest')
        self.assertFalse(label['ok'])
        self.assertEqual(label['stats'], ['212 lines', 'exit 1', '8 s'])
        self.assertNotIn('error', label)

    def test_a_command_that_never_ran_uses_the_plain_verb(self):
        label = self.label('run_command', {'command': 'ls /nope'},
                           {'error': 'Command working directory must be a directory.\nSecond line.'})
        self.assertEqual(label['title'], 'run ls /nope')
        self.assertEqual(label['error'], 'Command working directory must be a directory.')
        self.assertFalse(label['ok'])

    def test_error_is_capped(self):
        label = self.label('read_file', {'path': 'a.py'}, {'error': 'x' * 400})
        self.assertEqual(len(label['error']), T.ERROR_CAP)

    def test_edit_file_error(self):
        label = self.label('edit_file', {'path': 'x.py'},
                           {'error': 'old_string was not found in the file. Read the file again.'})
        self.assertEqual(label['title'], 'edit x.py')
        self.assertEqual(label['error'], 'old_string was not found in the file. Read the file again.')
        self.assertEqual(label['open'], {'type': 'fold'})
        self.assertNotIn('inline_diff', label)
        self.assertNotIn('merge', label)

    def test_background_and_handed_back_jobs(self):
        started = self.label('run_command', {'command': 'npm run dev', 'background': True},
                             {'output': '', 'still_running': True, 'job_id': 'job-2',
                              'duration_seconds': 2.0})
        self.assertEqual(started['kind'], 'job')
        self.assertEqual(started['title'], 'started job: npm run dev')
        self.assertIn('still running as job-2', started['stats'])
        handed = self.label('run_command', {'command': 'sleep 600'},
                            {'output': '', 'still_running': True, 'job_id': 'job-3'})
        self.assertEqual(handed['kind'], 'job')
        self.assertEqual(handed['title'], 'ran sleep 600')

    def test_job_tools(self):
        out = self.label('command_output', {'job_id': 'job-2', 'wait_seconds': 0},
                         {'output': 'a\nb\n', 'truncated': True, 'exit_code': 0})
        self.assertEqual(out['kind'], 'job')
        self.assertEqual(out['title'], 'read job-2 output')
        self.assertEqual(out['stats'], ['2 lines', 'exit 0', 'truncated'])
        stop = self.label('stop_command', {'job_id': 'job-2'},
                          {'output': '', 'exit_code': -15, 'stopped': True})
        self.assertEqual(stop['title'], 'stopped job-2')
        self.assertIn('stopped', stop['stats'])

    def test_read_file(self):
        label = self.label('read_file', {'path': 'backend/relay_core/agent.py'},
                           {'path': 'backend/relay_core/agent.py', 'content': 'a\n' * 48})
        self.assertEqual(label['kind'], 'read')
        self.assertEqual(label['running'], 'reading backend/relay_core/agent.py')
        self.assertEqual(label['title'], 'read backend/relay_core/agent.py')
        self.assertEqual(label['path'], 'backend/relay_core/agent.py')
        self.assertEqual(label['stats'], ['48 lines'])
        self.assertEqual(label['open'], {'type': 'file', 'path': 'backend/relay_core/agent.py'})
        self.assertEqual(label['merge'],
                         {'key': 'read', 'singular': 'file', 'plural': 'files', 'lines': 48})

    def test_a_long_path_shrinks_to_its_name(self):
        path = 'backend/relay_core/very/deep/directory/tree/somewhere/module.py'
        label = self.label('read_file', {'path': path}, {'content': 'a\n'})
        self.assertEqual(label['title'], 'read module.py')
        self.assertEqual(label['path'], path)

    def test_list_directory(self):
        entries = [{'name': f'f{i}', 'type': 'file'} for i in range(40)]
        label = self.label('list_directory', {'path': 'src'}, {'entries': entries, 'truncated': False})
        self.assertEqual(label['title'], 'listed src/')
        self.assertEqual(label['stats'], ['40 entries'])
        self.assertEqual(label['merge'],
                         {'key': 'list', 'singular': 'folder', 'plural': 'folders', 'entries': 40})
        root = self.label('list_directory', {'path': '.'}, {'entries': [], 'truncated': False})
        self.assertEqual(root['title'], 'listed ./')
        self.assertEqual(root['stats'], ['0 entries'])

    def test_write_file_new(self):
        label = self.label('write_file', {'path': 'x.py', 'content': 'a\n'},
                           {'path': 'x.py', 'written_bytes': 2, 'added': 48, 'removed': 0,
                            'created': True})
        self.assertEqual(label['kind'], 'edit')
        self.assertEqual(label['title'], 'wrote x.py')
        self.assertEqual(label['stats'], ['new', '48 lines'])
        self.assertEqual(label['open'], {'type': 'file', 'path': 'x.py'})

    def test_write_file_over_an_existing_file_is_an_edit(self):
        label = self.label('write_file', {'path': 'x.py', 'content': 'a\n'},
                           {'path': 'x.py', 'added': 3, 'removed': 1, 'created': False})
        self.assertEqual(label['title'], 'edited x.py')
        self.assertEqual(label['stats'], [f'+3 {MINUS}1'])
        self.assertTrue(label['inline_diff'])

    def test_edit_file_small_and_large(self):
        small = self.label('edit_file', {'path': 'x.py'},
                           {'path': 'x.py', 'added': 3, 'removed': 1, 'replacements': 1})
        self.assertEqual(small['title'], 'edited x.py')
        self.assertEqual(small['stats'], [f'+3 {MINUS}1'])
        self.assertTrue(small['inline_diff'])
        self.assertEqual(small['open'], {'type': 'fold'})
        big = self.label('edit_file', {'path': 'src/Pane.h'},
                         {'path': 'src/Pane.h', 'added': 212, 'removed': 87, 'replacements': 4})
        self.assertEqual(big['stats'], [f'+212 {MINUS}87', '4 replacements'])
        self.assertFalse(big['inline_diff'])
        self.assertEqual(big['open'], {'type': 'diff'})

    def test_the_twelve_line_boundary(self):
        for added, removed, inline in ((6, 6, True), (7, 6, False), (12, 0, True), (0, 13, False)):
            label = self.label('edit_file', {'path': 'x.py'},
                               {'added': added, 'removed': removed, 'replacements': 1})
            with self.subTest(added=added, removed=removed):
                self.assertIs(label['inline_diff'], inline)
                self.assertEqual(label['open']['type'], 'fold' if inline else 'diff')
        self.assertEqual(T.INLINE_DIFF_LINES, 12)

    def test_an_edit_that_changes_nothing(self):
        label = self.label('edit_file', {'path': 'x.py'}, {'added': 0, 'removed': 0, 'replacements': 1})
        self.assertEqual(label['stats'], ['no change'])

    def test_todos(self):
        label = self.label('update_todos', {'items': []}, {'ok': True, 'items': [], 'open': 3})
        self.assertEqual(label['kind'], 'plan')
        self.assertEqual(label['title'], 'updated todos')
        self.assertEqual(label['stats'], ['3 open'])
        self.assertEqual(label['open'], {'type': 'todos'})

    def test_write_plan(self):
        label = self.label('write_plan', {'title': 'Fix the tests', 'content': '# x'},
                           {'path': '/p/2026-09-18-fix.md', 'written': True})
        self.assertEqual(label['title'], 'wrote plan “Fix the tests”')
        self.assertEqual(label['open'], {'type': 'plan'})

    def test_subagents(self):
        started = self.label('agent', {'description': 'fix tests', 'prompt': 'go', 'background': True},
                             {'id': 'a1', 'type': 'general', 'status': 'running', 'background': True})
        self.assertEqual(started['kind'], 'agent')
        self.assertEqual(started['title'], 'started subagent “fix tests”')
        self.assertEqual(started['open'], {'type': 'subagent', 'id': 'a1'})
        self.assertNotIn('merge', started)
        message = self.label('agent_message', {'id': 'a1', 'text': 'hi'},
                             {'id': 'a1', 'delivered': 'next_step', 'status': 'running'})
        self.assertEqual(message['title'], 'messaged a1')
        self.assertEqual(message['open'], {'type': 'subagent', 'id': 'a1'})
        wait = self.label('agent_wait', {}, {'agents': [], 'timed_out': False})
        self.assertEqual(wait['title'], 'waited for the background subagents')

    def test_board_tools(self):
        moved = self.label('board_move_card', {'id': 'K7Q2', 'status': 'done', 'reason': 'shipped'},
                           {'id': 'K7Q2', 'status': 'done', 'moved': True})
        self.assertEqual(moved['kind'], 'board')
        self.assertEqual(moved['title'], 'moved card #K7Q2 → done')
        self.assertEqual(moved['open'], {'type': 'card', 'id': 'K7Q2'})
        created = self.label('board_create_card', {'tab': 'features', 'status': 'inbox',
                                                   'title': 'Concise tool lines', 'request': 'x'},
                             {'id': 'TK9C', 'created': True})
        self.assertEqual(created['title'], 'created card “Concise tool lines”')
        self.assertEqual(created['open'], {'type': 'card', 'id': 'TK9C'})
        read = self.label('board_read', {'id': '#K7Q2'}, {'id': 'K7Q2', 'hash': 'abc'})
        self.assertEqual(read['title'], 'read card #K7Q2')
        listed = self.label('board_list', {'tab': 'features'}, {'cards': [{}, {}], 'total': 2})
        self.assertEqual(listed['title'], 'searched the board')
        self.assertEqual(listed['stats'], ['2 cards'])
        comment = self.label('board_comment', {'id': 'K7Q2', 'kind': 'note', 'text': 'hi'},
                             {'id': 'K7Q2', 'entry_id': 'e1', 'kind': 'note'})
        self.assertEqual(comment['title'], 'commented on card #K7Q2 · note')

    def test_skills(self):
        loaded = self.label('load_skill', {'name': 'dataviz'},
                            {'skill': 'dataviz', 'content': 'x\n' * 10, 'files': ['a.py'],
                             'truncated': False})
        self.assertEqual(loaded['kind'], 'skill')
        self.assertEqual(loaded['title'], 'loaded skill dataviz')
        self.assertEqual(loaded['stats'], ['10 lines', '1 file'])
        read = self.label('read_skill_file', {'name': 'dataviz', 'path': 'refs/palette.md'},
                          {'skill': 'dataviz', 'path': 'refs/palette.md', 'content': 'a\nb\n',
                           'truncated': False})
        self.assertEqual(read['title'], 'read dataviz/refs/palette.md')
        self.assertEqual(read['merge']['key'], 'read')
        self.assertEqual(read['merge']['lines'], 2)

    def test_keybindings(self):
        label = self.label('set_keybinding', {'action': 'pane.split', 'keys': ['Ctrl+Shift+P']},
                           {'action': 'pane.split', 'keys': ['Ctrl+Shift+P'], 'conflicts': []})
        self.assertEqual(label['kind'], 'config')
        self.assertEqual(label['title'], 'bound pane.split to Ctrl+Shift+P')
        unbound = self.label('set_keybinding', {'action': 'pane.split', 'keys': []},
                             {'action': 'pane.split', 'keys': [], 'conflicts': []})
        self.assertEqual(unbound['title'], 'unbound pane.split')

    def test_type_into_program(self):
        typed = self.label('type_into_program', {'intent': 'answer yes', 'text': 'y', 'submit': True},
                           {'ok': True, 'typed': 'y', 'program': 'apt', 'screen': 'x'})
        self.assertEqual(typed['kind'], 'input')
        self.assertEqual(typed['title'], 'typed “y”')
        key = self.label('type_into_program', {'intent': 'quit', 'key': 'escape'},
                         {'ok': True, 'typed': '', 'program': 'vim', 'screen': 'x'})
        self.assertEqual(key['title'], 'typed <escape>')
        refused = self.label('type_into_program', {'intent': 'answer', 'text': 'hunter2'},
                             {'ok': False, 'refused': 'password', 'error': 'That prompt is masked.'})
        self.assertFalse(refused['ok'])
        self.assertEqual(refused['title'], 'type into the program')
        self.assertEqual(refused['error'], 'That prompt is masked.')

    def test_typed_text_is_never_longer_than_the_preview_shows(self):
        label = self.label('type_into_program', {'intent': 'paste', 'text': 'x' * 500},
                           {'ok': True, 'typed': 'x' * 500, 'program': 'sh', 'screen': ''})
        self.assertLessEqual(len(label['title']), T.SHORT_COMMAND + 12)

    def test_run_in_terminal(self):
        ran = self.label('run_in_terminal', {'command': 'ssh -t host', 'mode': 'run', 'intent': 'log in'},
                         {'ok': True, 'action': 'started', 'command': 'ssh -t host'})
        self.assertEqual(ran['kind'], 'run')
        self.assertEqual(ran['title'], 'ran ssh -t host in your terminal')
        prefilled = self.label('run_in_terminal',
                               {'command': 'rm -rf build', 'mode': 'prefill', 'intent': 'clean'},
                               {'ok': True, 'action': 'prefilled', 'command': 'rm -rf build',
                                'downgraded': True})
        self.assertEqual(prefilled['title'], 'prefilled rm -rf build')
        self.assertIn('prefilled instead', prefilled['stats'])

    def test_unknown_tool_falls_back(self):
        label = self.label('do_the_thing', {'target': 'the widget', 'count': 3}, {'ok': True})
        self.assertEqual(label['kind'], 'other')
        self.assertEqual(label['title'], 'do the thing the widget')
        self.assertEqual(label['running'], 'running do the thing')
        self.assertEqual(label['open'], {'type': 'fold'})

    def test_an_mcp_style_name_is_external(self):
        label = self.label('mcp__github__create_issue', {'repo': 'o/r', 'title': 'Bug'}, {'ok': True})
        self.assertEqual(label['kind'], 'external')
        self.assertEqual(label['title'], 'mcp github create issue o/r')

    def test_the_fallback_argument_is_short_plain_and_not_a_secret(self):
        long_arg = self.label('do_it', {'body': 'x' * 400, 'name': 'ok'}, {'ok': True})
        self.assertEqual(long_arg['title'], 'do it ok')
        multi = self.label('do_it', {'body': 'one\ntwo'}, {'ok': True})
        self.assertEqual(multi['title'], 'do it')
        secret = self.label('do_it', {'auth': 'token=abc123'}, {'ok': True})
        self.assertEqual(secret['title'], 'do it')

    def test_every_kind_is_declared(self):
        for name, args in (('run_command', {'command': 'ls'}), ('command_output', {'job_id': 'j'}),
                           ('read_file', {'path': 'a'}), ('list_directory', {'path': 'a'}),
                           ('write_file', {'path': 'a'}), ('edit_file', {'path': 'a'}),
                           ('agent', {'description': 'd'}), ('write_plan', {'title': 't'}),
                           ('update_todos', {}), ('load_skill', {'name': 's'}),
                           ('board_list', {}), ('set_keybinding', {'action': 'a', 'keys': []}),
                           ('type_into_program', {'text': 'y'}), ('mystery_tool', {}),
                           ('mcp__x__y', {})):
            with self.subTest(tool=name):
                self.assertIn(T.result_label(name, args, {'ok': True})['kind'], T.KINDS)

    def test_duration_only_from_a_second(self):
        for seconds, shown in ((0.4, None), (0.999, None), (1.0, '1 s'), (1.24, '1.2 s'),
                               (8.0, '8 s'), (9.96, '10 s'), (61.4, '61 s')):
            label = self.label('run_command', {'command': 'ls'},
                               {'output': '', 'exit_code': 0, 'duration_seconds': seconds})
            with self.subTest(seconds=seconds):
                if shown is None:
                    self.assertEqual(label['stats'], ['exit 0'])
                else:
                    self.assertEqual(label['stats'][-1], shown)

    def test_duration_falls_back_to_the_measured_milliseconds(self):
        label = self.label('read_file', {'path': 'a.py'}, {'content': 'a'}, ms=2400)
        self.assertEqual(label['stats'], ['1 lines'.replace('1 lines', '1 line'), '2.4 s'])

    def test_line_counts_use_thousands_separators(self):
        label = self.label('read_file', {'path': 'a.py'}, {'content': 'x\n' * 4100})
        self.assertEqual(label['stats'], ['4,100 lines'])
        self.assertEqual(label['merge']['lines'], 4100)

    def test_timed_out_and_truncated(self):
        label = self.label('run_command', {'command': 'ls'},
                           {'output': 'a\n', 'timed_out': True, 'truncated': True})
        self.assertFalse(label['ok'])
        self.assertIn('timed out', label['stats'])
        self.assertIn('truncated', label['stats'])

    def test_a_failed_call_never_merges(self):
        label = self.label('read_file', {'path': 'a.py'}, {'error': 'File is too large or binary.'})
        self.assertNotIn('merge', label)
        self.assertEqual(label['open'], {'type': 'fold'})

    def test_commands_edits_and_agents_never_merge(self):
        for name, args, result in (('run_command', {'command': 'ls'}, {'output': '', 'exit_code': 0}),
                                   ('edit_file', {'path': 'a'}, {'added': 1, 'removed': 1}),
                                   ('agent', {'description': 'd'}, {'id': 'a1'}),
                                   ('write_file', {'path': 'a'}, {'created': True, 'added': 1, 'removed': 0})):
            with self.subTest(tool=name):
                self.assertNotIn('merge', T.result_label(name, args, result))

    def test_started_label_is_the_present_tense_half(self):
        started = T.started_label('read_file', {'path': 'a.py'})
        self.assertEqual(started, {'kind': 'read', 'running': 'reading a.py', 'title': 'read a.py',
                                   'path': 'a.py',
                                   'merge': {'key': 'read', 'singular': 'file', 'plural': 'files'}})
        self.assertNotIn('stats', started)
        self.assertNotIn('ok', started)

    def test_started_label_knows_whether_a_write_replaces_a_file(self):
        self.assertEqual(T.started_label('write_file', {'path': 'x.py'}, existed=False)['title'],
                         'wrote x.py')
        self.assertEqual(T.started_label('write_file', {'path': 'x.py'}, existed=True)['title'],
                         'edited x.py')
        self.assertEqual(T.started_label('edit_file', {'path': 'x.py'}, existed=True)['running'],
                         'editing x.py')

    def test_labels_survive_rubbish(self):
        for name, args, result in (('run_command', None, None), (None, None, None),
                                   ('read_file', {'path': 42}, []), ('edit_file', {}, {'added': 'x'}),
                                   ('agent', {'description': None}, {'id': 7})):
            with self.subTest(tool=name):
                label = T.result_label(name, args, result)
                self.assertIn(label['kind'], T.KINDS)
                self.assertTrue(label['title'])
                self.assertIn(label['open']['type'],
                              ('fold', 'file', 'diff', 'subagent', 'card', 'plan', 'todos'))


class DetailTests(unittest.TestCase):
    def test_a_command_detail_is_ordered_sections(self):
        sections = T.detail('run_command', {'command': 'ls -la', 'cwd': 'src'},
                            {'output': 'a\nb\n', 'exit_code': 0})
        self.assertEqual([s['heading'] for s in sections], ['command', 'working directory', 'output'])
        self.assertEqual(sections[0], {'heading': 'command', 'style': 'code', 'text': 'ls -la'})
        self.assertEqual(sections[-1]['style'], 'output')
        for section in sections:
            self.assertIn(section['style'], T.DETAIL_STYLES)

    def test_a_write_detail_is_the_diff(self):
        diff = '--- a/x.py\n+++ b/x.py\n@@ -1 +1 @@\n-a\n+b\n'
        sections = T.detail('edit_file', {'path': 'x.py'}, {'added': 1, 'removed': 1}, diff=diff)
        self.assertEqual(sections, [{'heading': 'diff', 'style': 'diff', 'text': diff}])

    def test_a_write_detail_falls_back_to_the_legacy_preview(self):
        preview = ('EDIT FILE\n\n/w/x.py\n\n--- a/x.py\n+++ b/x.py\n@@ -1 +1 @@\n-a\n+b\n\n'
                   'Old bytes: 2; new bytes: 2.')
        sections = T.detail('edit_file', {'path': 'x.py'}, {'added': 1, 'removed': 1}, preview=preview)
        self.assertEqual(sections[0]['text'], '--- a/x.py\n+++ b/x.py\n@@ -1 +1 @@\n-a\n+b')

    def test_sections_are_capped_and_say_so(self):
        sections = T.detail('run_command', {'command': 'ls'}, {'output': 'x' * (T.DETAIL_OUTPUT_CAP + 10)})
        output = sections[-1]
        self.assertEqual(len(output['text']), T.DETAIL_OUTPUT_CAP)
        self.assertTrue(output['truncated'])

    def test_a_truncated_result_is_marked(self):
        sections = T.detail('read_file', {'path': 'a'}, {'content': 'a', 'truncated': True})
        self.assertTrue(sections[0]['truncated'])

    def test_the_caps_match_what_the_tools_store(self):
        self.assertEqual(T.DETAIL_OUTPUT_CAP, MAX_OUTPUT)
        self.assertEqual(T.DETAIL_TEXT_CAP, MAX_FILE)

    def test_an_error_gets_its_own_section(self):
        sections = T.detail('edit_file', {'path': 'x.py'}, {'error': 'old_string was not found.'})
        self.assertEqual(sections[-1], {'heading': 'error', 'style': 'error',
                                        'text': 'old_string was not found.'})

    def test_other_tools_show_their_arguments_never_raw_json(self):
        sections = T.detail('board_update_card', {'id': 'K7Q2', 'base_hash': 'abc'},
                            {'id': 'K7Q2', 'changes': ['status: done']})
        self.assertEqual(sections[0]['heading'], 'arguments')
        self.assertEqual(sections[0]['text'], 'id: K7Q2\nbase_hash: abc')
        self.assertEqual(sections[1], {'heading': 'changes', 'style': 'args', 'text': 'status: done'})

    def test_a_listing_detail_names_folders(self):
        sections = T.detail('list_directory', {'path': 'src'},
                            {'entries': [{'name': 'a', 'type': 'directory'},
                                         {'name': 'b.cpp', 'type': 'file'}], 'truncated': False})
        self.assertEqual(sections[0]['text'], 'a/\nb.cpp')

    def test_detail_never_raises(self):
        for name, args, result in (('run_command', None, None), (None, {}, {}),
                                   ('type_into_program', {'text': 'y'}, {'ok': True, 'screen': 's'}),
                                   ('agent', {'prompt': 'p'}, {'result': {'text': 'done'}})):
            with self.subTest(tool=name):
                for section in T.detail(name, args, result):
                    self.assertIn(section['style'], T.DETAIL_STYLES)
                    self.assertIsInstance(section['text'], str)


class SafeArgsTests(unittest.TestCase):
    def test_strings_are_capped(self):
        kept = T.safe_args('write_file', {'path': 'x.py', 'content': 'a' * (T.ARG_TEXT_CAP + 50)})
        self.assertEqual(len(kept['content']), T.ARG_TEXT_CAP)
        self.assertEqual(kept['path'], 'x.py')

    def test_scalars_and_structures_survive(self):
        kept = T.safe_args('run_command', {'command': 'ls', 'timeout_seconds': 30,
                                           'background': False, 'nothing': None})
        self.assertEqual(kept, {'command': 'ls', 'timeout_seconds': 30, 'background': False,
                                'nothing': None})
        self.assertEqual(T.safe_args('update_todos', {'items': [{'text': 'a', 'status': 'pending'}]}),
                         {'items': [{'text': 'a', 'status': 'pending'}]})

    def test_rubbish_is_a_dict(self):
        self.assertEqual(T.safe_args('x', None), {})
        self.assertEqual(T.safe_args('x', 'nonsense'), {})


if __name__ == '__main__':
    unittest.main()
