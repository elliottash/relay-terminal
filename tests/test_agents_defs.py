import json
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

from relay_core.agents_defs import (READ_ONLY_TOOLS, SUBAGENT_TOOLS, default_locations, load_catalog,
                                    parse_yaml)

ROOT = Path(__file__).resolve().parents[1]


def write(path: Path, text: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(text).lstrip("\n"), encoding="utf-8")


def md(name, description="Does things.", extra="", body="Prompt body."):
    return f"---\nname: {name}\ndescription: {description}\n{extra}---\n{body}\n"


class LocationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.ws = Path(self.temp.name) / "ws"
        self.home = Path(self.temp.name) / "home"
        self.ws.mkdir(); self.home.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def test_every_known_location_loads(self):
        expected = {}
        for index, (directory, tool) in enumerate(default_locations(self.ws, self.home)):
            name = f"agent{index}"
            if tool == "codex":
                write(directory / f"{name}.toml",
                      f'name = "{name}"\ndescription = "d"\ndeveloper_instructions = "Do it."\n')
            else:
                write(directory / f"{name}.md", md(name))
            expected[name] = (tool, directory)
        catalog = load_catalog(self.ws, home=self.home)
        for name, (tool, directory) in expected.items():
            self.assertIn(name, catalog.definitions, name)
            definition = catalog.definitions[name]
            self.assertEqual(definition.tool, tool)
            self.assertTrue(definition.source.startswith(str(directory.resolve())))
        self.assertIn("explore", catalog.definitions)
        self.assertIn("general", catalog.definitions)
        self.assertEqual(catalog.duplicates, [])
        paths = [str(d) for d, _ in default_locations(self.ws, self.home)]
        for suffix in (".relay/agents", ".config/relay/agents", ".claude/agents", ".opencode/agent",
                       ".opencode/agents", ".config/opencode/agent", ".config/opencode/agents",
                       ".codex/agents", ".gemini/agents", ".cursor/agents"):
            self.assertTrue(any(p.endswith(suffix) for p in paths), suffix)

    def test_first_source_wins_and_duplicates_reported(self):
        write(self.ws / ".relay/agents/reviewer.md", md("reviewer", "Relay version"))
        write(self.ws / ".claude/agents/reviewer.md", md("reviewer", "Claude version"))
        write(self.home / ".claude/agents/explore.md", md("explore", "My explore"))
        catalog = load_catalog(self.ws, home=self.home)
        self.assertEqual(catalog.definitions["reviewer"].description, "Relay version")
        self.assertEqual(catalog.definitions["explore"].description, "My explore")
        names = sorted((d["name"], d["kept"] == "builtin") for d in catalog.duplicates)
        self.assertEqual(names, [("explore", False), ("reviewer", False)])
        dup = next(d for d in catalog.duplicates if d["name"] == "explore")
        self.assertEqual(dup["source"], "builtin")
        self.assertTrue(any("duplicate" in w for w in catalog.warnings()))

    def test_explicit_dirs_only(self):
        write(self.ws / ".claude/agents/a.md", md("a"))
        custom = Path(self.temp.name) / "custom"
        write(custom / "b.md", md("b"))
        catalog = load_catalog(self.ws, [str(custom)], home=self.home)
        self.assertIn("b", catalog.definitions)
        self.assertNotIn("a", catalog.definitions)

    def test_invalid_files_are_skipped_with_reason(self):
        write(self.ws / ".claude/agents/nofm.md", "just text\n")
        write(self.ws / ".claude/agents/nodesc.md", "---\nname: nodesc\n---\nbody\n")
        write(self.ws / ".codex/agents/bad.toml", "name = \n")
        catalog = load_catalog(self.ws, home=self.home)
        reasons = {Path(s["path"]).name: s["reason"] for s in catalog.skipped}
        self.assertIn("frontmatter", reasons["nofm.md"])
        self.assertIn("description", reasons["nodesc.md"])
        self.assertIn("TOML", reasons["bad.toml"])


class FormatTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.ws = Path(self.temp.name) / "ws"
        self.home = Path(self.temp.name) / "home"
        self.ws.mkdir(); self.home.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def load(self):
        return load_catalog(self.ws, home=self.home)

    def test_claude_code_tools_string_list_and_disallowed(self):
        write(self.ws / ".claude/agents/reader.md", """
            ---
            name: reader
            description: >
              Reads code
              carefully.
            tools: Read, Glob, WebFetch
            model: sonnet
            effort: low
            maxTurns: 7
            background: true
            ---
            You read.
            """)
        write(self.ws / ".claude/agents/lister.md", """
            ---
            name: lister
            description: "Lists: files"
            tools:
              - Bash
              - LS
              - Edit
            disallowedTools: [Edit]
            ---
            body
            """)
        write(self.ws / ".claude/agents/all.md", md("all"))
        catalog = self.load()
        reader = catalog.definitions["reader"]
        self.assertEqual(reader.description, "Reads code carefully.")
        self.assertEqual(reader.tools, ("read_file", "list_directory"))
        self.assertEqual((reader.model, reader.effort, reader.max_steps, reader.background), ("sonnet", "low", 7, True))
        self.assertEqual(reader.prompt, "You read.")
        self.assertTrue(any("WebFetch" in w and "no Relay equivalent" in w for w in reader.warnings))
        lister = catalog.definitions["lister"]
        self.assertEqual(lister.description, "Lists: files")
        self.assertEqual(lister.tools, ("run_command", "list_directory"))
        self.assertEqual(catalog.definitions["all"].tools, SUBAGENT_TOOLS)

    def test_tool_mapping_edge_cases(self):
        write(self.ws / ".claude/agents/t.md", md("t", extra="tools: Grep, Bash(git:*), Write, Skill, mcp__x__y\n"))
        definition = self.load().definitions["t"]
        self.assertEqual(definition.tools, ("run_command", "write_file", "load_skill", "read_skill_file"))
        joined = " | ".join(definition.warnings)
        self.assertIn("full shell", joined)
        self.assertIn("patterns cannot be enforced", joined)
        self.assertIn("mcp__x__y", joined)
        # Foreign edit tools are Relay's edit_file; only names that create whole files add write_file.
        write(self.ws / ".claude/agents/e.md", md("e", extra="tools: Edit, MultiEdit\n"))
        write(self.ws / ".gemini/agents/g.md", "---\nname: g\ndescription: g\ntools:\n  - replace\n---\ng\n")
        catalog = self.load()
        self.assertEqual(catalog.definitions["e"].tools, ("edit_file",))
        self.assertEqual(catalog.definitions["g"].tools, ("write_file", "edit_file"))

    def test_opencode_markdown(self):
        write(self.ws / ".opencode/agent/review/security.md", """
            ---
            description: Security review
            mode: subagent
            model: moonshot/kimi-k3
            variant: high
            steps: 9
            tools:
              write: false
              webfetch: false
            permission:
              bash:
                "ls *": allow
                "*": ask
            ---
            Review.
            """)
        write(self.ws / ".opencode/agents/main.md", "---\ndescription: primary\nmode: primary\n---\nx\n")
        write(self.home / ".config/opencode/agent/off.md", "---\ndescription: off\ndisable: true\n---\nx\n")
        catalog = self.load()
        definition = catalog.definitions["review/security"]
        self.assertEqual(definition.tool, "opencode")
        # `write: false` turns off opencode's write tool only, so the agent keeps edit_file; a
        # permission short of "allow" (bash, here) withholds its tools entirely.
        self.assertEqual(definition.tools, ("read_file", "list_directory", "edit_file", "load_skill", "read_skill_file"))
        self.assertEqual((definition.model, definition.effort, definition.max_steps), ("moonshot/kimi-k3", "high", 9))
        self.assertTrue(any("per-pattern" in w for w in definition.warnings))
        self.assertNotIn("main", catalog.definitions)
        self.assertNotIn("off", catalog.definitions)
        self.assertEqual(len(catalog.skipped), 2)

    def test_codex_toml(self):
        write(self.home / ".codex/agents/pr-explorer.toml", '''
            name = "pr_explorer"
            description = "Explores PRs"
            model = "glm-5.3"
            model_reasoning_effort = "xhigh"
            sandbox_mode = "read-only"
            developer_instructions = """
            Stay read-only.
            """
            ''')
        definition = self.load().definitions["pr_explorer"]
        self.assertEqual(definition.tool, "codex")
        self.assertTrue(definition.read_only)
        self.assertNotIn("write_file", definition.tools)
        self.assertEqual(definition.effort, "max")
        self.assertEqual(definition.prompt, "Stay read-only.")

    def test_gemini_and_cursor(self):
        write(self.ws / ".gemini/agents/auditor.md", """
            ---
            name: auditor
            description: Audits
            kind: local
            tools:
              - read_file
              - grep_search
              - web_fetch
            max_turns: 20
            ---
            Audit.
            """)
        write(self.ws / ".gemini/agents/remote.md", "---\nname: remote\ndescription: r\nkind: remote\n---\n")
        write(self.home / ".cursor/agents/verifier.md", """
            ---
            name: verifier
            description: Verifies work
            model: fast
            readonly: true
            is_background: true
            ---
            Verify.
            """)
        catalog = self.load()
        auditor = catalog.definitions["auditor"]
        self.assertEqual(auditor.tools, ("run_command", "read_file"))
        self.assertEqual(auditor.max_steps, 20)
        self.assertNotIn("remote", catalog.definitions)
        verifier = catalog.definitions["verifier"]
        self.assertTrue(verifier.read_only)
        self.assertTrue(verifier.background)
        self.assertNotIn("write_file", verifier.tools)

    def test_builtins(self):
        catalog = self.load()
        explore = catalog.definitions["explore"]
        self.assertEqual(explore.tools, READ_ONLY_TOOLS)
        self.assertEqual(explore.effort, "low")
        self.assertEqual(catalog.definitions["general"].tools, SUBAGENT_TOOLS)
        self.assertEqual(explore.source, "builtin")

    def test_yaml_subset(self):
        data = parse_yaml('a: "x # not comment"  # comment\nb: [1, two]\nc: {d: true, e: null}\n'
                          'f: |\n  line1\n  line2\ng: 2.5\n')
        self.assertEqual(data, {"a": "x # not comment", "b": [1, "two"], "c": {"d": True, "e": None},
                                "f": "line1\nline2", "g": 2.5})


class WorkerAgentsListTests(unittest.TestCase):
    def test_agents_list_and_configure_count(self):
        with tempfile.TemporaryDirectory() as temp:
            ws, home = Path(temp) / "ws", Path(temp) / "home"
            ws.mkdir(); home.mkdir()
            write(ws / ".claude/agents/r.md", md("reviewer", extra="tools: Read, WebSearch\n"))
            write(home / ".claude/agents/r.md", md("reviewer", "second"))
            payload = "".join(json.dumps(m) + "\n" for m in [
                {"type": "agents_list", "id": "L", "workspace": str(ws)},
                {"type": "configure", "base_url": "http://127.0.0.1:1/v1", "model": "t", "api_key": "",
                 "workspace": str(ws)},
                {"type": "agents_list", "id": "L2"},
                {"type": "shutdown"}])
            env = {**os.environ, "HOME": str(home)}
            proc = subprocess.run([sys.executable, "-S", str(ROOT / "backend/worker.py")], input=payload, text=True,
                                  capture_output=True, timeout=10, cwd=ROOT, env=env)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            events = [json.loads(line) for line in proc.stdout.splitlines()]
            lists = [e for e in events if e["event"] == "agents"]
            self.assertEqual(len(lists), 2, events)
            for listing in lists:
                names = [item["name"] for item in listing["items"]]
                self.assertEqual(names, ["reviewer", "explore", "general"])
                self.assertEqual(listing["items"][0]["tools"], ["read_file"])
                self.assertEqual(listing["duplicates"][0]["name"], "reviewer")
                for key in ("name", "description", "source", "model", "tools"):
                    self.assertIn(key, listing["items"][0])
            configured = next(e for e in events if e["event"] == "configured")
            self.assertEqual(configured["agents"], 3)


if __name__ == "__main__":
    unittest.main()
