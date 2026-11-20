import copy
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import MagicMock, Mock, patch

HERE = Path(__file__).resolve().parents[1]/'computer'
sys.path.insert(0, str(HERE))
import upgrade
import paramiko


class Connections(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.home = Path(self.temp.name)
        self.session = self.home/'saved'
        self.session.mkdir()
        self.key = paramiko.ECDSAKey.generate()
        self.key.write_private_key_file(str(self.session/'zigma_key'))
        self.pin = paramiko.ECDSAKey.generate()
        known = paramiko.HostKeys()
        known.add('192.0.2.1', self.pin.get_name(), self.pin)
        known.save(str(self.session/'known_hosts'))
        (self.session/'connection.txt').write_text('SSH: root@192.0.2.1\n')

    def test_changed_ip_retains_pin(self):
        with patch('builtins.input', return_value='192.0.2.2'):
            host, key, pin = upgrade.select_connection(self.session)
        self.assertEqual(host, '192.0.2.2')
        self.assertEqual(pin, self.pin)
        self.assertEqual(upgrade.load_key(key), self.key)
        self.assertIn('192.0.2.1', (self.session/'connection.txt').read_text())

    def test_default_ip_is_shown(self):
        with patch('builtins.input', return_value='') as prompt:
            self.assertEqual(upgrade.select_connection(self.session)[0], '192.0.2.1')
        self.assertIn('192.0.2.1', prompt.call_args.args[0])

    def test_missing_pin_refused(self):
        (self.session/'known_hosts').unlink()
        with self.assertRaises(OSError):
            upgrade.saved_connection(self.session)

    def test_external_key_session_can_be_reused(self):
        key = self.home/'external key'
        (self.session/'zigma_key').rename(key)
        (self.session/'connection.txt').write_text(f'SSH: root@192.0.2.1\nPrivate key: {key}\n')
        self.assertEqual(upgrade.saved_connection(self.session)[1], key)

    def test_without_session(self):
        with patch.object(Path, 'home', return_value=self.home), patch('builtins.input', side_effect=['0', '192.0.2.7', str(self.session/'zigma_key')]):
            host, key, pin = upgrade.select_connection(None)
        self.assertEqual(host, '192.0.2.7')
        self.assertIsNone(pin)

    def test_invalid_ip_refused(self):
        with patch('builtins.input', return_value='127.0.0.1; touch /tmp/bad'), self.assertRaises(ValueError):
            upgrade.select_connection(self.session)

    def test_encrypted_key(self):
        self.key.write_private_key_file(str(self.session/'zigma_key'), password='fixture')
        with patch('getpass.getpass', return_value='fixture'):
            self.assertEqual(upgrade.load_key(self.session/'zigma_key'), self.key)

    def test_identity_changed_is_rejected(self):
        # The connection uses Paramiko's reject policy with only the saved pin.
        from nor_backup import connect
        fake = type('Robot', (), dict(host='192.0.2.2', host_key=self.pin, key=self.key))()
        with patch('paramiko.SSHClient') as client:
            client.return_value.get_host_keys.return_value = paramiko.HostKeys()
            connect(fake)
            self.assertEqual(client.return_value.get_host_keys()['192.0.2.2'][self.pin.get_name()], self.pin)
            client.return_value.set_missing_host_key_policy.assert_not_called()


class Reconnection(unittest.TestCase):
    def test_temporary_disconnect_recovers(self):
        robot = Mock()
        robot.command.side_effect = [TimeoutError(), EOFError(), paramiko.SSHException('banner'), 'ready']
        with patch.object(upgrade.time, 'sleep'):
            self.assertEqual(upgrade.transaction_state(robot, '/tmp/stage', time.monotonic()+30), 'ready')
        self.assertEqual(robot.command.call_count, 4)

    def test_deadline_bounds_disconnect(self):
        robot = Mock()
        robot.command.side_effect = TimeoutError()
        with self.assertRaises(TimeoutError):
            upgrade.transaction_state(robot, '/tmp/stage', time.monotonic()-1)
        self.assertEqual(robot.command.call_count, 1)

    def test_identity_authentication_and_remote_errors_are_fatal(self):
        for error in (ValueError('host key changed'), paramiko.AuthenticationException('denied'), RuntimeError('remote failure')):
            robot = Mock()
            robot.command.side_effect = error
            with self.assertRaises(type(error)):
                upgrade.transaction_state(robot, '/tmp/stage', time.monotonic()+30)
            self.assertEqual(robot.command.call_count, 1)


class UpgradeFlow(unittest.TestCase):
    def test_absent_web_file_has_no_fake_backup(self):
        self.presence = 'absent'
        self.test_lost_launch_poll_and_accept_responses_do_not_repeat_transaction()

    def test_lost_launch_poll_and_accept_responses_do_not_repeat_transaction(self):
        from contextlib import ExitStack
        with tempfile.TemporaryDirectory() as tmp, ExitStack() as stack:
            home = Path(tmp)
            robot = Mock()
            calls = []
            state = 'ready'
            disconnected = False
            def command(cmd, **kwargs):
                nonlocal state, disconnected
                calls.append(cmd)
                if cmd.startswith('test ! -L ') and 'echo present' in cmd:
                    return getattr(self, 'presence', 'present')
                if 'mktemp -d' in cmd:
                    return '/tmp/zigma-upgrade.fixture'
                if cmd == 'cat /proc/sys/kernel/random/boot_id':
                    return 'fixture-boot'
                if cmd.startswith('nohup '):
                    raise EOFError()
                if cmd.startswith('cat /tmp/zigma-upgrade.fixture/state'):
                    if not disconnected:
                        disconnected = True
                        raise TimeoutError()
                    return state
                if cmd.endswith(' accept'):
                    state = 'accepted'
                    raise TimeoutError()
                if '/done && echo done' in cmd:
                    return 'done'
                return ''
            robot.command.side_effect = command
            inventory = {'items': [{'name': 'fixture', 'kind': 'binary', 'sha256': 'newsha'}]}
            for target, value in [
                ('select_connection', ('192.0.2.1', home/'key', Mock())),
                ('load_key', Mock()), ('connect', MagicMock()), ('upload', None),
                ('idle', None), ('firmware_gate', 'true'),
                ('read_file', (b'old', '600')),
                ('read_bytes', json.dumps(inventory).encode()),
                ('payload', {'/root/zigma-free/src/app.js': (b'new', 0o644)}),
                ('api', {'items': [dict(inventory['items'][0], status='verified')]}),
            ]:
                stack.enter_context(patch.object(upgrade, target, return_value=value))
            stack.enter_context(patch.object(Path, 'home', return_value=home))
            stack.enter_context(patch('wizard.Robot', return_value=robot))
            stack.enter_context(patch.object(upgrade.time, 'sleep'))
            upgrade.upgrade()
            self.assertEqual(sum(cmd.startswith('nohup ') for cmd in calls), 1)
            self.assertEqual(sum(cmd.endswith(' accept') for cmd in calls), 1)
            self.assertFalse(any('rollback.now' in cmd for cmd in calls))
            self.assertEqual(len(list(home.glob('Zigma/*/verified.json'))), 1)
            if getattr(self, 'presence', 'present') == 'absent':
                session = next(home.glob('Zigma/*'))
                self.assertIn(' - 644 absent ', (session/'files.list').read_text())
                self.assertFalse((session/'0.old.gz').exists())



class Transaction(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='rc10-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.stage = self.root/'stage'
        self.stage.mkdir()
        self.base = self.root/'root/zigma-free'
        self.base.mkdir(parents=True)
        (self.root/'root/.zigma-install').mkdir()
        (self.base/'src').mkdir()
        self.target = self.base/'src/app.js'
        self.target.write_bytes(b'old')
        self.target.chmod(0o640)
        service = self.base/'zigma-free-service'
        service.write_text('#!/bin/sh\nexit 0\n');service.chmod(0o700)
        boot = Path('/proc/sys/kernel/random/boot_id').read_text()
        (self.stage/'boot').write_text(boot)
        self.oldsha = hashlib.sha256(b'old').hexdigest()
        self.newsha = hashlib.sha256(b'new').hexdigest()
        (self.stage/'files.list').write_text(f'0 {self.target} 640 644 {self.oldsha} {self.newsha}\n')
        for name, data in [('old', b'old'), ('new', b'new')]:
            (self.stage/f'0.{name}.gz').write_bytes(gzip.compress(data))
        script = (HERE/'upgrade-transaction.sh').read_text()
        # Filesystem/process boundary substitution only; transaction logic unchanged.
        script = script.replace('/root', str(self.root/'root')).replace('/tmp/zigma-upgrade.*', str(self.stage))
        start = script.index('identity() {'); end = script.index('\ncheckpath()', start)
        script = script[:start]+"identity() { echo fixture-native; }\n"+script[end:]
        script = script.replace('"$n" -lt 240', '"$n" -lt 2')
        self.script = self.stage/'transaction.sh'; self.script.write_text(script)

    def start(self):
        self.log = (self.stage/'log').open('w')
        self.addCleanup(self.log.close)
        process = subprocess.Popen(['sh', str(self.script), str(self.stage), 'run'], stdout=self.log, stderr=self.log)
        self.addCleanup(lambda: process.poll() is None and process.kill())
        return process

    def ready(self, process):
        deadline = time.monotonic()+4
        while time.monotonic()<deadline:
            if (self.stage/'state').exists() and (self.stage/'state').read_text().strip() == 'ready': return
            if process.poll() is not None: break
            time.sleep(.02)
        self.fail((self.stage/'log').read_text())

    def test_legacy_install_without_directory(self):
        directory = self.root/'root/.zigma-install'
        directory.rmdir()
        process = self.start(); self.ready(process)
        self.assertTrue(directory.is_dir())
        subprocess.run(['sh', str(self.script), str(self.stage), 'accept'], check=True)
        self.assertEqual(process.wait(timeout=5), 0)

    def test_lock_directory_reports_failure(self):
        (self.root/'root/.zigma-install/lock').mkdir()
        self.assertNotEqual(self.start().wait(timeout=5), 0)
        self.assertEqual((self.stage/'state').read_text().strip(), 'failed')
        self.assertEqual(self.target.read_bytes(), b'old')

    def new_asset(self):
        self.target.unlink()
        (self.stage/'0.old.gz').unlink()
        (self.stage/'files.list').write_text(f'0 {self.target} - 644 absent {self.newsha}\n')

    def test_new_asset_accept(self):
        self.new_asset()
        process = self.start(); self.ready(process)
        subprocess.run(['sh', str(self.script), str(self.stage), 'accept'], check=True)
        self.assertEqual(process.wait(timeout=5), 0)
        self.assertEqual(self.target.read_bytes(), b'new')
        self.assertEqual(self.target.stat().st_mode & 0o777, 0o644)

    def test_new_asset_rollback_removes_file(self):
        self.new_asset()
        process = self.start(); self.ready(process)
        (self.stage/'rollback.now').touch()
        process.wait(timeout=5)
        self.assertFalse(self.target.exists())
        self.assertEqual((self.stage/'state').read_text().strip(), 'rolled-back')

    def test_new_asset_external_change_not_removed(self):
        self.new_asset()
        process = self.start(); self.ready(process)
        self.target.write_bytes(b'external')
        (self.stage/'rollback.now').touch()
        process.wait(timeout=5)
        self.assertEqual(self.target.read_bytes(), b'external')
        self.assertEqual((self.stage/'state').read_text().strip(), 'rollback-failed')

    def test_new_asset_appearing_before_install_is_preserved(self):
        self.new_asset()
        self.target.write_bytes(b'external')
        self.assertNotEqual(self.start().wait(timeout=5), 0)
        self.assertEqual(self.target.read_bytes(), b'external')

    def test_new_asset_dangling_symlink_refused(self):
        self.new_asset()
        self.target.symlink_to(self.base/'missing')
        self.assertNotEqual(self.start().wait(timeout=5), 0)
        self.assertTrue(self.target.is_symlink())

    def test_accept(self):
        process = self.start();self.ready(process)
        subprocess.run(['sh', str(self.script), str(self.stage), 'accept'], check=True)
        self.assertEqual(process.wait(timeout=5), 0)
        self.assertEqual(self.target.read_bytes(), b'new')
        self.assertTrue((self.stage/'done').exists())

    def test_disconnect_rolls_back(self):
        process = self.start()
        process.wait(timeout=6)
        self.assertEqual(self.target.read_bytes(), b'old')
        self.assertEqual(self.target.stat().st_mode & 0o777, 0o640)
        self.assertEqual((self.stage/'state').read_text().strip(), 'rolled-back')

    def test_requested_rollback(self):
        process = self.start();self.ready(process)
        (self.stage/'rollback.now').touch()
        process.wait(timeout=5)
        self.assertEqual(self.target.read_bytes(), b'old')

    def test_modified_old_file_refused_before_write(self):
        self.target.write_bytes(b'other')
        self.assertNotEqual(self.start().wait(timeout=5), 0)
        self.assertEqual(self.target.read_bytes(), b'other')
        self.assertEqual((self.stage/'state').read_text().strip(), 'failed')

    def test_corrupt_upload_refused(self):
        (self.stage/'0.new.gz').write_bytes(gzip.compress(b'bad'))
        self.assertNotEqual(self.start().wait(timeout=5), 0)
        self.assertEqual(self.target.read_bytes(), b'old')

    def test_pending_install_refused(self):
        (self.root/'root/.zigma-install/pending').touch()
        self.assertNotEqual(self.start().wait(timeout=5), 0)
        self.assertEqual(self.target.read_bytes(), b'old')

    def test_changed_target_not_overwritten_during_rollback(self):
        process = self.start();self.ready(process)
        self.target.write_bytes(b'external change')
        (self.stage/'rollback.now').touch()
        process.wait(timeout=5)
        self.assertEqual(self.target.read_bytes(), b'external change')
        self.assertEqual((self.stage/'state').read_text().strip(), 'rollback-failed')

    def test_restart_failure_restores_partial_install(self):
        service = self.base/'zigma-free-service'
        marker = self.stage/'failed-once'
        service.write_text(f'#!/bin/sh\nif test ! -e "{marker}"; then touch "{marker}"; exit 1; fi\nexit 0\n')
        self.assertNotEqual(self.start().wait(timeout=5), 0)
        self.assertEqual(self.target.read_bytes(), b'old')
        self.assertEqual((self.stage/'state').read_text().strip(), 'rolled-back')

    def test_boot_change_refuses_all_writes(self):
        (self.stage/'boot').write_text('different-boot\n')
        self.assertNotEqual(self.start().wait(timeout=5), 0)
        self.assertEqual(self.target.read_bytes(), b'old')

    def test_accept_rejects_changed_bytes(self):
        process = self.start();self.ready(process)
        self.target.write_bytes(b'other')
        result = subprocess.run(['sh', str(self.script), str(self.stage), 'accept'])
        self.assertNotEqual(result.returncode, 0)
        (self.stage/'rollback.now').touch()
        process.wait(timeout=5)
        self.assertFalse((self.stage/'done').exists())

    def test_symlink_refused(self):
        target = self.base/'other';target.write_bytes(b'old')
        self.target.unlink();self.target.symlink_to(target)
        self.assertNotEqual(self.start().wait(timeout=5), 0)
        self.assertEqual(target.read_bytes(), b'old')


if __name__ == '__main__':
    unittest.main()
