"""Try-it can find and stage the native macOS app without Linux UI tools."""
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
from relay_core import tryit_protocol as TI


class MacTryItTests(unittest.TestCase):
    def setUp(self):
        native = mock.patch.object(TI, "WINDOWS", False)
        native.start()
        self.addCleanup(native.stop)

    def test_private_bash_stage_command_quotes_paths(self):
        with mock.patch.object(sys, 'platform', 'darwin'), \
                mock.patch.dict(os.environ, RELAY_BASH='/Applications/My Relay.app/Contents/Resources/bash/bin/bash'):
            command = TI._stage_command('/tmp/fixture one/stage.sh')
            self.assertEqual(command, "'/Applications/My Relay.app/Contents/Resources/bash/bin/bash' '/tmp/fixture one/stage.sh'")

    def test_installed_binary_then_built_bundle(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(sys, 'platform', 'darwin'):
            root = Path(directory)
            contents = root / 'installed' / 'Relay.app' / 'Contents'
            binary = contents / 'MacOS' / 'relay'
            binary.parent.mkdir(parents=True)
            binary.touch()
            interpreter = contents / 'Resources' / 'python' / 'bin' / 'python3'
            with mock.patch.object(sys, 'executable', str(interpreter)):
                self.assertEqual(TI._app_binary(root), binary)
                built = root / 'build' / 'Relay.app' / 'Contents' / 'MacOS' / 'relay'
                built.parent.mkdir(parents=True)
                built.touch()
                self.assertEqual(TI._app_binary(root), built)

    def test_native_gui_brief(self):
        with mock.patch.object(sys, 'platform', 'darwin'):
            brief = TI._platform_brief()
            self.assertNotIn('Xvfb', brief)
            self.assertNotIn('xdotool', brief)
            self.assertIn('Accessibility', brief)
            self.assertIn('RELAY_KEYRING=off', brief)
            self.assertIn('stage.sh', brief)
            self.assertNotIn('stage.ps1', brief)


if __name__ == '__main__':
    unittest.main()
