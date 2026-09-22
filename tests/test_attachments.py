"""SSH attachments must never resolve a same-named file on this machine."""
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from relay_core import attachments

class RemoteAttachmentTests(unittest.TestCase):
    def setUp(self):
        self.session = {'host': 'test-host', 'cwd': '/remote', 'reachable': True, 'control_path': '/socket'}

    def test_collision_uses_remote_bytes_and_provenance(self):
        with tempfile.TemporaryDirectory() as root:
            Path(root, 'same.txt').write_text('LOCAL MUST NOT LEAK')
            with patch('relay_core.remote_session.require_socket'), patch('relay_core.remote_files.run', return_value=subprocess.CompletedProcess([], 0, b'REMOTE', b'')) as run:
                result = attachments.load([{'host': 'test-host', 'path': 'same.txt'}], root, remote_session=self.session)
            self.assertEqual(result[0]['content'], 'REMOTE')
            self.assertEqual(result[0]['path'], 'test-host:/remote/same.txt')
            self.assertEqual(run.call_args.kwargs['cwd'], '/remote')

    def test_unverified_or_missing_host_never_reads_local_file(self):
        for session in (None, {**self.session, 'reachable': False}, {**self.session, 'host': 'other'}):
            with self.subTest(session=session), patch('relay_core.remote_files.run') as run:
                with self.assertRaises(ValueError):
                    attachments.load([{'host': 'test-host', 'path': '/etc/hostname'}], '.', remote_session=session)
                run.assert_not_called()

    def test_truncation_and_binary_refusal(self):
        with patch('relay_core.remote_session.require_socket'), patch('relay_core.remote_files.run', return_value=subprocess.CompletedProcess([], 0, b'x' * (attachments.PER_FILE_CAP + 1), b'')):
            result = attachments.load([{'host': 'test-host', 'path': 'large.txt'}], '.', remote_session=self.session)
            self.assertTrue(result[0]['truncated'])
            self.assertEqual(len(result[0]['content']), attachments.PER_FILE_CAP)
        with patch('relay_core.remote_session.require_socket'), patch('relay_core.remote_files.run', return_value=subprocess.CompletedProcess([], 0, b'a\0b', b'')):
            with self.assertRaisesRegex(ValueError, 'binary'):
                attachments.load([{'host': 'test-host', 'path': 'binary'}], '.', remote_session=self.session)
