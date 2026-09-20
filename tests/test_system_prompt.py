"""The assembled system prompt and the tool list: what they cost, and that they do not move.

Both are input on every provider request of every step of every turn, so a byte here is paid
hundreds of times in a session — and a byte that *changes* is worse than a byte that is merely
there: every provider prompt cache and llama.cpp's prefix cache key on the prefix, so one line
that moves in the middle throws away the cached work for everything after it. These tests pin
that the prompt is byte-identical across the turns of a conversation and across worker restarts
given the same configuration, and that what does change is at the end (#GMCF, #PF4K).

`docs/qa_evidence/2026-09-20-perf-fixes/prompt/promptsize.py` prints the same prompt broken down
per section and per tool; these are its assertions.
"""
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest import mock

from relay_core import activity_tools, app_tools, board as board_mod, board_tools
from relay_core import instructions as instructions_mod, skills as skills_mod
from relay_core.agent import Agent
from relay_core.keybindings import KeybindingCatalog
from relay_core.provider import ProviderConfig

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ProviderConfig('http://127.0.0.1:12345/v1', 'mock', '')
BOARD_CONFIG = """\
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes}]
columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
"""
# The `app` block a pane sends (§30.2) — rows and actions, not the empty one a bare Agent has:
# the tool list is measured here, and an empty catalog hides what a pane really carries.
APP = {"tab": "t1", "writes_enabled": True,
       "options": [{"id": "appearance.theme", "section": "appearance", "section_label": "Appearance",
                    "label": "Theme", "kind": "choice", "value": "dark", "settable": True,
                    "choices": [{"value": "dark", "label": "Dark"}, {"value": "light", "label": "Light"}]},
                   {"id": "agent.turn_limit", "section": "agent", "section_label": "Agent",
                    "label": "Turn limit", "kind": "number", "value": 40, "min": 1, "max": 200,
                    "settable": True}],
       "actions": [{"key": "settings.open", "section": "Relay", "label": "Open settings",
                    "agent_safe": True}]}
PANE_TOKEN = '3f2504e0-4f89-11d3-9a0c-0305e82c3301'


def keybinding_catalog(root: Path) -> KeybindingCatalog:
    """The catalog every pane's `configure` carries: `src/Keymap.h`'s registry, keys and all.

    Built from the real registry, not a stub of three, because this file measures the tool list
    and `set_keybinding` used to *be* that registry — 9,837 bytes of it, invisible to a fixture
    that left `keybindings` out (#GMCF). `docs/qa_evidence/2026-09-20-perf-fixes/prompt/promptsize.py`
    reads it the same way.
    """
    source = (ROOT / 'src' / 'Keymap.h').read_text(encoding='utf-8')
    actions = [{'id': m.group(1), 'description': m.group(2),
                'keys': re.findall(r'QStringLiteral\("([^"]+)"\)', m.group(3))}
               for m in re.finditer(
                   r'^\s*add\("([^"]+)",\s*"[^"]*",\s*"((?:[^"\\]|\\.)*)",\s*\{(.*?)\}\);', source, re.M)]
    return KeybindingCatalog(str(root / 'conf' / 'keybindings.json'), actions)


def write(path: Path, text: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')


def make_workspace(root: Path) -> tuple[Path, Path, Path]:
    """A fixed workspace, Switchboard and skill library, so nothing here depends on the machine."""
    workspace = root / 'ws'
    workspace.mkdir()
    write(workspace / 'AGENTS.md', 'Project rules: run the tests before you say it works.\n')
    repo = root / 'repo'
    write(repo / 'issues' / board_mod.BOARD_CONFIG, BOARD_CONFIG)
    library = root / 'skills'
    write(library / 'alpha' / 'SKILL.md',
          '---\nname: alpha\ndescription: Renames a project everywhere, including its git remotes '
          'and its CI files, in one pass.\n---\nBody\n')
    write(library / 'beta' / 'SKILL.md',
          '---\nname: beta\ndescription: A very long description that keeps going well past any '
          'reasonable line length, so the catalogue has to clip it before it reaches the prompt at '
          'all, and here is even more of it.\nshort: Draw a flame graph of a slow test run.\n'
          '---\nBody\n')
    write(library / 'gamma' / 'SKILL.md',
          '---\nname: gamma\ndescription: Short one. And a second sentence with the trigger words '
          'invoice, receipt and VAT in it.\n---\nBody\n')
    return workspace, repo, library


def build_agent(workspace: Path, repo: Path, library: Path, *, board: bool = True) -> Agent:
    """A pane agent carrying every section `Agent.system_prompt` can put in the prompt, and
    every tool a pane's `configure` brings with it: the app block and the keybinding catalog."""
    agent = Agent(CONFIG, str(workspace), lambda event: None,
                  keybindings=keybinding_catalog(repo))
    agent.executor.skills = skills_mod.SkillIndex.load([library])
    agent.instructions = instructions_mod.load({'project_auto': True}, str(workspace))
    agent.app = app_tools.AppTools(app_tools.AppCatalog.from_request(APP),
                                   app_tools.AppBridge(lambda event: None),
                                   keybindings=lambda: agent.executor.keybindings)
    agent.activity = activity_tools.ActivityTools(agent)
    if board:
        agent.board = board_tools.BoardTools(
            board_mod.Board(repo / 'issues', repo), emit=lambda event: None, autonomy='auto',
            state_path=repo / '.relay' / 'rate.json', pane_token=PANE_TOKEN,
            context=board_tools.ToolContext(actor='agent', model='m', pane='2'))
        agent.board.begin_turn('t-1')
    agent.refresh_system_prompt()
    return agent


class PromptFixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.workspace, self.repo, self.library = make_workspace(Path(self.temp.name))

    def tearDown(self):
        self.temp.cleanup()

    def agent(self, *, board=True) -> Agent:
        return build_agent(self.workspace, self.repo, self.library, board=board)


# What a worker restart looks like: a new process, a new PYTHONHASHSEED, the same configuration.
RESTART = textwrap.dedent("""
    import hashlib, json, sys, tempfile
    from pathlib import Path
    sys.path.insert(0, sys.argv[1] + '/tests')
    from test_system_prompt import build_agent, make_workspace
    with tempfile.TemporaryDirectory(dir=sys.argv[2]) as temp:
        workspace, repo, library = make_workspace(Path(temp))
        agent = build_agent(workspace, repo, library)
        out = agent.system_prompt().replace(str(workspace), '<ws>').replace(str(repo), '<repo>')
        out += json.dumps(agent.tools(), ensure_ascii=False)
        print(hashlib.sha256(out.encode('utf-8')).hexdigest())
""")


class StabilityTests(PromptFixture):
    def test_the_prompt_and_the_tool_list_do_not_move_between_turns(self):
        # Nothing in either may carry a clock, a counter, a random id or a set's iteration order:
        # the second turn of a conversation has to hit the provider's cache of the first.
        agent = self.agent()
        first = (agent.messages[0]['content'], json.dumps(agent.tools(), ensure_ascii=False))
        for _ in range(3):
            agent.refresh_system_prompt()
            self.assertEqual((agent.messages[0]['content'],
                              json.dumps(agent.tools(), ensure_ascii=False)), first)

    def test_the_prompt_and_the_tool_list_are_the_same_after_a_restart(self):
        digests = set()
        for seed in ('0', '1', '12345'):
            env = {**os.environ, 'PYTHONHASHSEED': seed, 'RELAY_KEYRING': 'off',
                   'PYTHONPATH': str(ROOT / 'backend')}
            done = subprocess.run([sys.executable, '-c', RESTART, str(ROOT), self.temp.name],
                                  capture_output=True, text=True, env=env, timeout=180)
            self.assertEqual(done.returncode, 0, done.stderr)
            digests.add(done.stdout.strip())
        self.assertEqual(len(digests), 1, digests)

    def test_what_changes_while_the_conversation_runs_is_at_the_end(self):
        # Claiming a card changes what the prompt says this pane holds. That sentence is the tail,
        # so everything a provider has already cached — SYSTEM, the skills, the policy — still
        # matches byte for byte after the claim.
        agent = self.agent()
        before = agent.system_prompt()
        card = agent.board.run('board_create_card', {'tab': 'features', 'status': 'inbox',
                                                     'title': 'A card', 'request': 'do the thing'})
        agent.board.run('board_claim', {'id': card['id']})
        agent.refresh_system_prompt()
        after = agent.messages[0]['content']
        self.assertNotEqual(after, before)
        head = before.rstrip('\n').rsplit('\n', 1)[0]      # everything above the claim line
        self.assertTrue(after.startswith(head), 'a claim changed something other than the tail')
        self.assertIn(f"You hold: #{card['id']}.", after.splitlines()[-1])

    def test_switching_mode_only_changes_the_tail_too(self):
        agent = self.agent()
        build = agent.system_prompt()
        agent.set_mode('plan')
        shared = os.path.commonprefix([build, agent.system_prompt()])
        self.assertGreater(len(shared), len(build) - 200)

    def test_the_prompt_carries_no_clock_and_no_identifier(self):
        # The two that would be easiest to add without noticing. A session id or a timestamp
        # anywhere but the tail costs the whole cached prefix below it, every turn.
        agent = self.agent()
        prompt = agent.system_prompt()
        self.assertNotIn(agent.session_id, prompt)
        self.assertNotIn(str(os.getpid()), prompt.split('Chosen workspace')[0])


class PolicyCacheTests(unittest.TestCase):
    """`policy_text` used to read and re-regex ~5 KB of Markdown on every prompt build (#GMCF)."""

    def setUp(self):
        board_tools._POLICY_CACHE = None
        self.reads: list[str] = []

    def tearDown(self):
        board_tools._POLICY_CACHE = None

    def counting_reads(self):
        real = pathlib.Path.read_text
        reads = self.reads

        def read_text(path, *args, **kwargs):
            if path.name == 'board_policy.md':
                reads.append(str(path))
            return real(path, *args, **kwargs)

        return mock.patch.object(pathlib.Path, 'read_text', read_text)

    def test_the_policy_file_is_read_once_and_the_text_is_the_same(self):
        with self.counting_reads():
            first = board_tools.policy_text()
            for _ in range(20):
                self.assertEqual(board_tools.policy_text(), first)
        self.assertEqual(len(self.reads), 1, self.reads)
        self.assertIn('Switchboard rules', first)
        self.assertNotIn('<!--', first)       # the file's provenance comment is not for the model

    def test_an_edited_policy_still_takes_effect(self):
        # The cache is keyed on the file's own mtime and size, so a policy edited in this checkout
        # reaches the next prompt build without restarting the worker.
        with self.counting_reads():
            board_tools.policy_text()
            self.assertEqual(len(self.reads), 1)
            (mtime, size), text = board_tools._POLICY_CACHE
            board_tools._POLICY_CACHE = ((mtime - 1, size), text)     # as an edit would leave it
            board_tools.policy_text()
            self.assertEqual(len(self.reads), 2, self.reads)


class SizeTests(PromptFixture):
    """A budget, so the next section added to the prompt is a decision and not an accident."""

    def test_the_prompt_and_the_tools_stay_within_their_budgets(self):
        agent = self.agent(board=False)
        prompt = len(agent.system_prompt().encode('utf-8'))
        tools = len(json.dumps(agent.tools(), ensure_ascii=False).encode('utf-8'))
        # Measured 2026-09-20 (#GMCF): 8.2 KB of prompt with three skills, 15.4 KB of tools.
        self.assertLess(prompt, 10 * 1024, f'system prompt grew to {prompt} bytes')
        self.assertLess(tools, 18 * 1024, f'tool schemas grew to {tools} bytes')
        board = len(self.agent().system_prompt().encode('utf-8'))
        self.assertLess(board, 14 * 1024, f'prompt with a Switchboard grew to {board} bytes')

    def test_the_skills_catalogue_is_one_trigger_line_per_skill(self):
        agent = self.agent(board=False)
        section = agent.executor.skills.prompt_section()
        lines = [line for line in section.splitlines() if line.startswith('- ')]
        self.assertEqual(len(lines), 3)
        for line in lines:
            self.assertLessEqual(len(line), 2 + 40 + skills_mod.MAX_SHORT + 2, line)
        # The frontmatter's own `short:` wins over the description it would otherwise be clipped from.
        self.assertIn('- beta: Draw a flame graph of a slow test run.', section)
        self.assertNotIn('and here is even more of it', section)

    def test_the_tool_list_carries_no_per_user_catalogue(self):
        """A schema is the same for every user, or it is paid for on every request and caches nothing.

        `set_keybinding` was the exception: 9,960 B of the user's own 92 actions and their current
        keys, rewritten whenever one was rebound (#GMCF decision 1). The catalogues live in tool
        *results* now — `app_action_list` — so this pins the shape and not only the bytes, and the
        budget is the pane's whole tool list: 16.4 KB, where it was 25.9 KB.
        """
        agent = self.agent(board=False)
        specs = agent.tools()
        self.assertIn('set_keybinding', [s['function']['name'] for s in specs])
        catalog = agent.executor.keybindings
        self.assertGreater(len(catalog.actions), 50, 'the fixture is not carrying a real registry')
        text = json.dumps(specs, ensure_ascii=False)
        listed = [action.id for action in catalog.actions.values() if action.id in text]
        self.assertLessEqual(len(listed), 1, f'the tool list names actions: {listed}')
        for action in catalog.actions.values():
            for key in action.keys:
                self.assertNotIn(f'[{key}]', text, f'{action.id}\'s keys are in the tool list')
        for spec in specs:
            self.assertLess(len(json.dumps(spec, ensure_ascii=False).encode('utf-8')), 3 * 1024,
                            f'{spec["function"]["name"]} is bigger than any schema should be')
        size = len(text.encode('utf-8'))
        self.assertLess(size, 17 * 1024 + 512, f'the pane tool list grew to {size} bytes')


if __name__ == '__main__':
    unittest.main()
