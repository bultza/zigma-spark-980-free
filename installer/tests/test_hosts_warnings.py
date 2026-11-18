"""Hosts failures remain advisory without suppressing user cancellation."""
import contextlib
import importlib.util
import io
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

ROOT=Path(__file__).resolve().parents[1]
with patch.dict('sys.modules', {'nor_backup':SimpleNamespace(connect=Mock(),upload=Mock())}):
    spec=importlib.util.spec_from_file_location('hosts_warnings',ROOT/'computer/hosts_guard.py')
    hosts=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(hosts)

class HostsWarnings(unittest.TestCase):
    def test_post_ssh_failure_warns_without_credentials(self):
        robot=SimpleNamespace(directory=Path('/tmp'))
        for error in (OSError('secret password'), RuntimeError('remote failed'), ValueError('bad pin')):
            with patch.object(hosts,'connect',side_effect=error), contextlib.redirect_stdout(io.StringIO()) as out:
                self.assertFalse(hosts.apply(robot))
            self.assertIn('WARNING',out.getvalue())
            self.assertIn('continuing installation',out.getvalue())
            self.assertNotIn('secret password',out.getvalue())

    def test_pairing_unexpected_failure_is_advisory(self):
        modules={'paramiko':SimpleNamespace(SSHException=RuntimeError),'wizard':SimpleNamespace()}
        with patch.dict('sys.modules',modules), patch.object(hosts.socket,'create_connection',side_effect=ValueError('bad pairing state')), contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertFalse(hosts.before_pairing(Path('/tmp')))
        self.assertIn('WARNING',out.getvalue())

    def test_success_and_user_interrupt(self):
        self.assertTrue(hosts.best_effort(lambda:True)())
        @hosts.best_effort
        def cancel():
            raise KeyboardInterrupt()
        with self.assertRaises(KeyboardInterrupt):cancel()

if __name__=='__main__':unittest.main()
