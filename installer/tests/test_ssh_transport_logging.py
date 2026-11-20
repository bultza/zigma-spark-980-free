"""Failed/late SSH handshakes remain diagnostic without terminal tracebacks."""
import ast
import io
import logging
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import MagicMock, patch
import paramiko

source=Path(__file__).resolve().parents[1]/'computer/wizard.py'
node=next(n for n in ast.parse(source.read_text()).body if isinstance(n,ast.ClassDef) and n.name=='Robot')
namespace={'time':time,'trace':lambda *_:None}
exec(compile(ast.Module(body=[node],type_ignores=[]),str(source),'exec'),namespace)
Robot=namespace['Robot']

class SSHTransportLogging(unittest.TestCase):
    def test_failure_propagates_once_and_late_log_stays_in_session_file(self):
        with tempfile.TemporaryDirectory() as directory:
            robot=Robot('192.0.2.1',None,Path(directory))
            terminal=io.StringIO();handler=logging.StreamHandler(terminal)
            root=logging.getLogger();root.addHandler(handler)
            client=MagicMock();client.__enter__.return_value=client
            client.connect.side_effect=paramiko.SSHException('No existing session')
            try:
                with patch.object(paramiko,'SSHClient',return_value=client):
                    with self.assertRaisesRegex(paramiko.SSHException,'No existing session'):
                        robot.command('read-only-fixture')
                client.connect.assert_called_once();client.exec_command.assert_not_called()
                client.__exit__.assert_called_once()
                options=client.connect.call_args.kwargs
                self.assertGreater(options['timeout'],options['banner_timeout'])
                self.assertEqual(options['auth_timeout'],8)
                logger=logging.getLogger(client.set_log_channel.call_args.args[0])
                logger.error('Late banner failure: Bad file descriptor')
                self.assertEqual(terminal.getvalue(),'')
                self.assertIn('Late banner failure', (Path(directory)/'ssh-transport.log').read_text())
                self.assertIs(robot.ssh_logger(),logger);self.assertEqual(len(logger.handlers),1)
                # Authentication failures must still reach the caller, not be retried here.
                client.connect.side_effect=paramiko.AuthenticationException('bad key')
                with patch.object(paramiko,'SSHClient',return_value=client):
                    with self.assertRaises(paramiko.AuthenticationException):robot.command('read-only-fixture')
            finally:
                root.removeHandler(handler)
                for h in robot.ssh_logger().handlers[:]:h.close();robot.ssh_logger().removeHandler(h)
