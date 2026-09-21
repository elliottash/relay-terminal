# SPDX-License-Identifier: AGPL-3.0-or-later
"""Native Windows commands and parser; PS7 parser also runs on Linux with RELAY_POWERSHELL."""
import os
import shutil
import subprocess
import tempfile
import threading
import unittest
from pathlib import Path
from relay_core import jobs, router

PWSH = os.environ.get('RELAY_POWERSHELL') or shutil.which('pwsh')

@unittest.skipUnless(PWSH, 'PowerShell 7 is required')
class PowerShellParserTests(unittest.TestCase):
    def setUp(self):
        self.old = os.environ.get('RELAY_POWERSHELL')
        os.environ['RELAY_POWERSHELL'] = PWSH

    def tearDown(self):
        if self.old is None:
            os.environ.pop('RELAY_POWERSHELL', None)
        else:
            os.environ['RELAY_POWERSHELL'] = self.old

    def test_cmdlet_pipeline_and_case_insensitive_alias(self):
        for text in ('Get-Item . | Select-Object Name', 'WRITE-output "héllo"', '$x = 2; $x + 1'):
            self.assertTrue(router.powershell_runnable(text, (), None, None)[0], text)

    def test_invalid_syntax_and_unknown_command(self):
        self.assertFalse(router.powershell_runnable('Write-Output "', (), None, None)[2])
        result = router.powershell_runnable('relay_command_that_does_not_exist', (), None, None)
        self.assertEqual(result[:3], (False, 'command not found: relay_command_that_does_not_exist', True))

    def test_syntax_check_never_executes_substitutions(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'must-not-exist'
            text = "Write-Output $(New-Item -ItemType File -Path '" + str(path).replace("'", "''") + "')"
            self.assertTrue(router.powershell_runnable(text, (), None, root)[0])
            self.assertFalse(path.exists())

    def test_events(self):
        subprocess.run([PWSH, '-NoLogo', '-NoProfile', '-File',
                        str(Path(__file__).with_name('powershell-integration.ps1'))], check=True)

@unittest.skipUnless(os.name == 'nt' and PWSH, 'Native Windows and PowerShell 7 are required')
class WindowsJobsTests(unittest.TestCase):
    def setUp(self):
        self.table = jobs.JobTable()
        self.root = tempfile.TemporaryDirectory()
        self.env = dict(os.environ, RELAY_POWERSHELL=PWSH)

    def tearDown(self):
        self.table.stop_all()
        self.root.cleanup()

    def start(self, command):
        return self.table.start(command, self.root.name, self.env)

    def test_unicode_multiline_and_native_exit_code(self):
        job = self.start("Write-Output 'héllo 世界'\n& $env:ComSpec /c exit 7")
        self.assertTrue(job.done.wait(15))
        self.assertEqual(job.exit_code, 7)
        self.assertIn('héllo 世界', self.table.peek(job)['output'])

    def test_failure_and_explicit_exit(self):
        for command, status in (("Write-Error 'bad'", 1), ('exit 13', 13)):
            job = self.start(command)
            self.assertTrue(job.done.wait(15))
            self.assertEqual(job.exit_code, status)

    def test_streaming_and_cancellation(self):
        job = self.start("Write-Output 'started'; Start-Sleep 120")
        for _ in range(100):
            if 'started' in self.table.peek(job)['output']:
                break
            threading.Event().wait(.1)
        self.assertIn('started', self.table.peek(job)['output'])
        self.table.stop(job)
        self.assertTrue(job.done.wait(5))
        self.assertTrue(job.stopped)

    def test_parent_exit_cleans_descendants(self):
        job = self.start("$p = Start-Process $env:ComSpec -ArgumentList '/c ping -n 120 127.0.0.1 > nul' -PassThru; "
                         "Write-Output $p.Id")
        self.assertTrue(job.done.wait(15))
        child = int(self.table.peek(job)['output'].strip())
        result = subprocess.run([PWSH, '-NoProfile', '-Command',
                                 f'if (Get-Process -Id {child} -ErrorAction SilentlyContinue) {{ exit 1 }}'],
                                capture_output=True)
        self.assertEqual(result.returncode, 0)

if __name__ == '__main__':
    unittest.main()
