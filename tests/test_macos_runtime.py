"""Native Keychain and macOS platform wiring; no real provider credentials."""
import importlib.machinery
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import uuid

from relay_core import jobs, keystore, router

ROOT = Path(__file__).resolve().parents[1]
loader = importlib.machinery.SourceFileLoader('mac_relay_open', str(ROOT / 'scripts' / 'relay-open'))
spec = importlib.util.spec_from_loader(loader.name, loader)
relay_open = importlib.util.module_from_spec(spec)
loader.exec_module(relay_open)


class MacPlatformTests(unittest.TestCase):
    @unittest.skipIf(os.name == 'nt', 'Unix shell branch')
    def test_private_bash_is_used_for_commands_and_syntax(self):
        private = '/Applications/Relay.app/Contents/Resources/bash/bin/bash'
        self.assertEqual(jobs.shell_argv('echo okay', {'RELAY_BASH': private})[0], private)
        with mock.patch.dict(os.environ, RELAY_BASH=private), \
                mock.patch.object(router.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, '', '')) as run:
            self.assertEqual(router.bash_syntax('echo okay'), (True, ''))
            self.assertEqual(run.call_args.args[0][0], private)

    @unittest.skipIf(os.name == 'nt', 'macOS platform branch')
    def test_keychain_dispatch_keeps_environment_precedence(self):
        with mock.patch.object(sys, 'platform', 'darwin'), \
                mock.patch.dict(os.environ, RELAY_OPENAI_API_KEY='local-env', RELAY_KEYRING='off'), \
                mock.patch.object(keystore, '_mac_keyring') as keyring:
            self.assertEqual(keystore.lookup('openai'), 'local-env')
            keyring.assert_not_called()
        with mock.patch.object(sys, 'platform', 'darwin'), \
                mock.patch.object(keystore, '_mac_keyring') as keyring:
            keystore.store('openai', 'test-key')
            if os.name != 'nt':
                keyring.assert_called_once_with('store', 'openai', 'test-key')

    @unittest.skipIf(os.name == 'nt', 'macOS platform branch')
    def test_open_uses_qt_application_support_discovery(self):
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(sys, 'platform', 'darwin'), \
                mock.patch.dict(os.environ, RELAY_OPEN_SOCKET=''), \
                mock.patch.object(os.path, 'expanduser', return_value=directory):
            path = Path(directory) / 'relay' / 'open-socket'
            path.parent.mkdir()
            path.write_text('/private/tmp/relay-open-test/open.sock')
            self.assertEqual(relay_open.socket_address(), '/private/tmp/relay-open-test/open.sock')

    @unittest.skipIf(os.name == 'nt', 'macOS platform branch')
    def test_native_open_fallback(self):
        with tempfile.NamedTemporaryFile() as file, \
                mock.patch.object(sys, 'platform', 'darwin'), \
                mock.patch.object(relay_open, 'send', return_value=False), \
                mock.patch.object(relay_open.subprocess, 'Popen') as start, \
                mock.patch.object(sys, 'argv', ['relay-open', file.name]):
            self.assertEqual(relay_open.main(), 0)
            self.assertEqual(start.call_args.args[0], ['/usr/bin/open', file.name])

    def test_remote_input_is_refused_without_password_state(self):
        if str(ROOT) not in sys.path:
            sys.path.insert(0, str(ROOT))
        from remote import gui_host, terminal, wire
        source = object.__new__(gui_host.GuiPaneSource)
        with mock.patch.object(sys, 'platform', 'darwin'):
            self.assertIsNone(source.secret_state('pane'))
            with self.assertRaises(wire.WireError):
                source.secret_prompt('pane')
            with self.assertRaises(wire.WireError):
                terminal.secret_prompt(123)

    @unittest.skipUnless(sys.platform == 'darwin', 'native macOS Keychain')
    def test_native_keychain_roundtrip(self):
        from relay_core import maccredentials
        service = 'org.relayterminal.Relay.test.' + uuid.uuid4().hex
        account = 'native-test'
        try:
            self.assertEqual(maccredentials.lookup(service, account), '')
            maccredentials.store(service, account, 'test-secret-é-雪')
            self.assertEqual(maccredentials.lookup(service, account), 'test-secret-é-雪')
            maccredentials.store(service, account, 'replacement')
            self.assertEqual(maccredentials.lookup(service, account), 'replacement')
            self.assertTrue(maccredentials.remove(service, account))
            self.assertFalse(maccredentials.remove(service, account))
        finally:
            maccredentials.remove(service, account)


if __name__ == '__main__':
    unittest.main()
