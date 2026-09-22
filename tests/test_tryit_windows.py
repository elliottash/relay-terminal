"""Windows Try-it fixtures use native paths, PowerShell and the installed binary."""
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

from relay_core import tryit_protocol as TI


class NativeTryItTests(unittest.TestCase):
    def test_drive_and_unc_paths_remain_paths_even_with_spaces(self):
        for path in [r'C:\Users\me\proof.png', r'C:\Users\My Name\proof.png',
                     'C:/Users/My Name/proof.png', r'\\server\share\proof.png']:
            with self.subTest(path=path):
                self.assertEqual(TI._open_line(f'Open: `{path}`'), (path, 'path'))
        self.assertEqual(TI._open_line("Open: `pwsh -File 'C:/My Files/stage.ps1'`")[1], 'command')

    def test_native_temp_does_not_need_unix_user_id(self):
        with mock.patch.object(TI, 'WINDOWS', True), mock.patch.dict(os.environ, {}, clear=True), \
                mock.patch.object(TI.tempfile, 'gettempdir', return_value=tempfile.gettempdir()), \
                mock.patch.object(os, 'getuid', create=True, side_effect=AssertionError('Unix API')):
            self.assertEqual(TI._run_dir(), str(Path(tempfile.gettempdir()) / 'relay-tryit'))
        with mock.patch.object(TI, 'WINDOWS', True), mock.patch.dict(os.environ, RELAY_TRYIT_ROOT='custom-root'):
            self.assertEqual(TI._run_dir(), 'custom-root')

    def test_native_staging_and_prompt(self):
        with tempfile.TemporaryDirectory(prefix="tryit's ") as directory, \
                mock.patch.object(TI, 'WINDOWS', True), mock.patch.object(TI, 'STAGE_SCRIPT', 'stage.ps1'):
            root = Path(directory)
            staged = root / TI.EVIDENCE_ROOT / '2026-09-21-verify-ABCD'
            staged.mkdir(parents=True)
            (staged / 'stage.sh').write_text('unix only')
            self.assertEqual(TI.verify_staging(root, 'ABCD')['stage'], '')
            (staged / 'stage.ps1').write_text('Write-Output ready')
            found = TI.verify_staging(root, 'ABCD')
            self.assertTrue(found['stage'].endswith('stage.ps1'))
            card = SimpleNamespace(body='', title='Native fixture', path=root / 'issues' / 'card.md')
            tools = SimpleNamespace(board=SimpleNamespace(repo=root, root=root/'issues', card_by_id=lambda _: card),
                                    run=lambda *args: {'body': 'A native card', 'entries': []})
            prompt = TI.tryit_prompt(tools, 'ABCD', staged)
            self.assertIn("run `pwsh -NoLogo -NoProfile -File '", prompt)
            self.assertIn('Windows UI Automation', prompt)
            self.assertIn('stage.ps1', prompt)
            self.assertNotIn('Xvfb', prompt)
            self.assertNotIn('xdotool', prompt)
            self.assertNotIn('stage.sh', prompt)
            self.assertIn("name''s", TI._stage_command("name's/stage.ps1"))

    def test_native_binary_build_then_private_install(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(TI, 'WINDOWS', True):
            root = Path(directory)
            built = root / 'build' / 'Release' / 'relay.exe'
            installed = root / 'install' / 'bin' / 'relay.exe'
            interpreter = root / 'install' / 'runtime' / 'python' / 'python.exe'
            installed.parent.mkdir(parents=True)
            installed.touch()
            with mock.patch.object(TI.sys, 'executable', str(interpreter)):
                self.assertEqual(TI._app_binary(root), installed)
                built.parent.mkdir(parents=True)
                built.touch()
                self.assertEqual(TI._app_binary(root), built)

    def test_linux_defaults_are_unchanged(self):
        with mock.patch.object(TI, 'WINDOWS', False):
            self.assertEqual(TI._app_binary(Path('/project')), Path('/project/build/relay'))
            self.assertEqual(TI._stage_command('docs/stage.sh'), 'bash docs/stage.sh')
            self.assertIn('Xvfb', TI._platform_brief())


if __name__ == '__main__':
    unittest.main()
