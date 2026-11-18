import ast
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'computer'))
import ssh_output

class UpgradeErrors(unittest.TestCase):
    def test_empty_eof_is_visible_and_saved(self):
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stderr(io.StringIO()) as out:
            saved = ssh_output.report_exception(EOFError(), tmp)
            self.assertIn('ERROR: EOFError: SSH connection closed', out.getvalue())
            self.assertEqual(json.loads(saved.read_text())['type'], 'EOFError')
            self.assertEqual(saved.stat().st_mode & 0o777, 0o600)

    def test_messages_are_redacted(self):
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stderr(io.StringIO()) as out:
            saved = ssh_output.report_exception(ValueError('token=secretfixture Bearer privatefixture'), tmp)
            for text in (out.getvalue(), saved.read_text()):
                self.assertNotIn('secretfixture', text)
                self.assertNotIn('privatefixture', text)

    def test_save_failure_does_not_hide_error(self):
        with patch.object(Path, 'mkdir', side_effect=OSError()), contextlib.redirect_stderr(io.StringIO()) as out:
            self.assertIsNone(ssh_output.report_exception(TimeoutError(), '/unused'))
        self.assertIn('ERROR: TimeoutError:', out.getvalue())
        self.assertIn('Cannot save', out.getvalue())

    def test_long_script_uses_stdin(self):
        tree = ast.parse((ROOT/'computer/wizard.py').read_text())
        node = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'Robot')
        namespace = {'time': time, 'trace': lambda *a: None}
        exec(compile(ast.Module(body=[node], type_ignores=[]), 'Robot', 'exec'), namespace)
        robot = namespace['Robot']('192.0.2.1', Mock(), Path('/unused'))
        robot.host_key = Mock()
        client = Mock();stdin,stdout,stderr = Mock(),Mock(),Mock()
        client.exec_command.return_value = stdin,stdout,stderr
        script = '# fixture\n'*2000
        with patch('paramiko.SSHClient') as constructor, patch('paramiko.HostKeys'), patch.object(ssh_output, 'collect', return_value=(b'PASS',b'',0,False,[4,0])):
            constructor.return_value.__enter__.return_value = client
            self.assertEqual(robot.command('sh -s',script=script), 'PASS')
        client.exec_command.assert_called_once_with('sh -s',timeout=15)
        stdin.write.assert_called_once_with(script)
        stdin.channel.shutdown_write.assert_called_once()
        stdin.close.assert_not_called()

    def test_real_idle_states_and_active_rejection(self):
        import upgrade
        for state in ('Standby', 'PileCharging', 'DirCharging', 'Hibernating'):
            with patch.object(upgrade, 'api', return_value={'working_status': state, 'error': 'NoError'}):
                upgrade.idle(None)
        for state, error in [('AutoClean','NoError'),('BackCharging','NoError'),('Hibernating','SensorError'),('unknown','NoError')]:
            with patch.object(upgrade, 'api', return_value={'working_status': state, 'error': error}), self.assertRaisesRegex(ValueError, 'status='+state):
                upgrade.idle(None)
