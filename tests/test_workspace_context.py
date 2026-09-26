"""Queue workspace adapters use real Git leases and leave legacy mode alone."""
import os
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))
from relay_core import trees, workspace_context, guest_launch
from relay_core.board_tools import find_board_root
from relay_core.scratch import keep_root
from relay_core.subagents import SubagentFactory
from relay_core.agents_defs import AgentDefinition
from relay_core.provider import ProviderConfig
from relay_core.integration_service import IntegrationService


def git(root, *args):
    subprocess.run(["git", *args], cwd=root, check=True, capture_output=True)


class WorkspaceContextTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "project"
        self.root.mkdir()
        self.state = Path(self.temp.name) / "relay"
        git(self.root, "init", "-b", "main")
        git(self.root, "config", "user.email", "test@example.com")
        git(self.root, "config", "user.name", "Test")
        (self.root / "AGENTS.md").write_text("test\n")
        (self.root / ".board").mkdir()
        (self.root / ".board" / "board.yaml").write_text("version: 1\n")
        (self.root / ".relay").mkdir()
        (self.root / ".relay" / "project.toml").write_text(
            'version = 1\n[workspace]\nexclude = [".board"]\n'
            '[verification]\ncommands = [["python3", "-c", "pass"]]\n')
        (self.root / "source.txt").write_text("base\n")
        git(self.root, "add", ".")
        git(self.root, "commit", "-m", "base")
        trees.register_repo(self.root, state_root=self.state)

    def test_two_children_are_distinct_and_canonical(self):
        with patch.dict(os.environ, {"RELAY_STATE_HOME": str(self.state.parent),
                                      "RELAY_LAND_ROOT": str(Path(self.temp.name) / "land")}, clear=False):
            IntegrationService(self.root, state_root=self.state,
                               land_root=Path(self.temp.name) / "land").activate()
            # A later target config cannot silently replace the accepted workspace policy.
            (self.root / ".relay" / "project.toml").write_text(
                'version = 1\n[workspace]\nexclude = [".board"]\nmax_workspaces = 1\n'
                '[verification]\ncommands = [["python3", "-c", "pass"]]\n')
            git(self.root, "add", ".relay/project.toml")
            git(self.root, "-c", "core.hooksPath=/dev/null", "commit", "-m", "later proposed policy")
            git(self.root, "branch", "-f", "main", "HEAD")
            # The direct adapter takes an explicit state root; environment based tools use
            # XDG_STATE_HOME/relay, which is made equivalent here.
            first = workspace_context.prepare(str(self.root), "parent:child:one", state_root=self.state)
            second = workspace_context.prepare(str(self.root), "parent:child:two", state_root=self.state)
            self.assertNotEqual(first["execution_cwd"], second["execution_cwd"])
            self.assertEqual(first["execution_cwd"], workspace_context.prepare(
                str(self.root), "parent:child:one", state_root=self.state)["execution_cwd"])
            self.assertEqual(first["execution_cwd"], workspace_context.validate_prepared(
                first, str(self.root), "parent:child:one", state_root=self.state)["execution_cwd"])
            self.assertEqual(first["board_root"], str(self.root / ".board"))
            self.assertEqual(find_board_root(first["execution_cwd"]), self.root / ".board")
            self.assertEqual(keep_root(first["execution_cwd"]), self.root / ".relay" / "work")
            self.assertFalse((Path(first["execution_cwd"]) / ".board").exists())
            self.assertEqual(first["repo_id"], second["repo_id"])
            self.assertEqual(workspace_context.queue_status(str(self.root),
                                                           state_root=self.state)["repo_id"], first["repo_id"])
            self.assertEqual(workspace_context.environment(first)["RELAY_WORKSPACE_ID"], first["workspace_id"])
            factory = SubagentFactory(ProviderConfig("http://127.0.0.1:12345/v1", "mock", ""),
                                      first["execution_cwd"], provider_factory=lambda config: object())
            factory.workspace_identity = first
            child_a, _, _ = factory(AgentDefinition("developer", "Develop"), None, None,
                                    lambda event: None, "one")
            child_b, _, _ = factory(AgentDefinition("developer", "Develop"), None, None,
                                    lambda event: None, "two")
            self.assertNotEqual(str(child_a.executor.workspace.root), str(child_b.executor.workspace.root))
            self.assertNotEqual(str(child_a.executor.workspace.root), first["execution_cwd"])
            launch = guest_launch.command_line(
                "codex", str(Path(self.temp.name) / "runtime"), first["execution_cwd"],
                home=self.temp.name, tree_status=first, session="parent:child:one")
            self.assertEqual(launch["execution_cwd"], first["execution_cwd"])
            self.assertEqual(launch["env"]["RELAY_BOARD_ROOT"], str(self.root / ".board"))
            self.assertIn("relay-land submit", Path(launch["memory_file"]).read_text())
            cli_env = {**os.environ, "PYTHONPATH": str(Path(__file__).resolve().parents[1] / "backend")}
            cli = subprocess.run([sys.executable, "-m", "relay_core.workspace_context", "prepare",
                                  "--project", str(self.root), "--session", "parent:child:one",
                                  "--state-root", str(self.state)], env=cli_env,
                                 text=True, capture_output=True, check=True)
            self.assertEqual(json.loads(cli.stdout)["workspace_id"], first["workspace_id"])

    def test_failure_closed_and_legacy(self):
        old = workspace_context.prepare(str(self.root), "", state_root=self.state)
        self.assertEqual(old["state"], "legacy")
        self.assertEqual(old["execution_cwd"], str(self.root))
        trees.configure_repo(self.root, state_root=self.state, mode="queue")
        with self.assertRaises(workspace_context.WorkspacePreparationError):
            workspace_context.prepare(str(self.root), "", state_root=self.state)
        with self.assertRaises(workspace_context.WorkspacePreparationError):
            workspace_context.validate_prepared({"workspace_id": "wrong"},
                                                str(self.root), "token", state_root=self.state)


if __name__ == "__main__":
    unittest.main()
