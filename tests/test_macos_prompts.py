"""macOS main, delegated and routing prompts describe the native shell accurately."""
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock
from relay_core import prompt_profiles


class MacPromptTests(unittest.TestCase):
    def test_darwin_context_keeps_bash_and_names_bsd_tools(self):
        with mock.patch.object(sys, 'platform', 'darwin'), mock.patch.object(os, 'name', 'posix'):
            text = prompt_profiles.platform_prompt('You work inside a Linux terminal. Use Bash.')
        self.assertIn('native macOS', text)
        self.assertIn('BSD system tools', text)
        self.assertIn('Bash', text)
        self.assertNotIn('inside a Linux terminal', text)
        self.assertNotIn('PowerShell', text)

    def test_all_agent_entry_prompts_use_native_host(self):
        backend = str(Path(__file__).resolve().parents[1] / 'backend')
        code = """import sys,json,os,urllib.request
sys.path.insert(0,sys.argv[1])
sys.platform='darwin'
from relay_core import agent,prompt_profiles,route_assist,subagents
print(json.dumps([agent.SYSTEM,prompt_profiles.SYSTEM_SHORT,route_assist.SYSTEM,subagents.SUBAGENT_SYSTEM]))
"""
        if os.name == 'nt':
            self.skipTest('Native Windows selects its own OS branch')
        result = subprocess.run([sys.executable, '-c', code, backend], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        for prompt in json.loads(result.stdout):
            self.assertIn('native macOS', prompt)
            self.assertIn('BSD system tools', prompt)
            self.assertNotIn('inside a Linux terminal', prompt)
            self.assertNotIn("Linux terminal's input box", prompt)

    def test_windows_route_context_is_not_linux(self):
        with mock.patch.object(os, 'name', 'nt'):
            text = prompt_profiles.platform_prompt("You route a Linux terminal's input box. Use Bash.")
        self.assertIn('native Windows', text)
        self.assertIn('PowerShell 7', text)
        self.assertNotIn("Linux terminal's input box", text)


if __name__ == '__main__':
    unittest.main()
