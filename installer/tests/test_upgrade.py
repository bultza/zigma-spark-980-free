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


class ManagedPayload(unittest.TestCase):
    def setUp(self):
        self.full = {}
        rows = []
        for name in upgrade.INVENTORY_NAMES:
            path = '/root/bin/'+name if name in ('zigmactl', 'zigma-time-sync') else upgrade.BASE+'/'+name
            script = name in upgrade.SCRIPT_NAMES
            source = upgrade.SCRIPT_FILES[path] if script else ('tools/zigmactl' if name == 'zigmactl' else 'stack/'+name)
            data = ('#!/bin/sh\n# new '+name+'\n').encode()
            self.full[source] = (data, 0o700)
            rows.append(dict(name=name, path=path, kind='script' if script else 'binary', sha256=hashlib.sha256(data).hexdigest(), version='new'))
        for path, source in upgrade.SCRIPT_FILES.items():
            self.full.setdefault(source, (b'#!/bin/sh\nexit 0\n', 0o700))
        for name in upgrade.TRANSPORT_FILES:
            self.full['stack/'+name] = (b'fixture transport asset', 0o644)
        for name in upgrade.WEB_NAMES:
            self.full['stack/src/'+name] = (b'fixture web asset', 0o644)
        rows.append(dict(name='S98zigma-free', path='/root/etc/init.d/S98zigma-free', kind='script', sha256='new-hook'))
        self.full['stack/release.json'] = (json.dumps({'items':rows}).encode(), 0o600)
        self.old = copy.deepcopy({'items':rows})
        for row in self.old['items']:
            row['sha256'] = 'old'; row['version'] = 'old'
        self.original = copy.deepcopy(self.old)

    def payload(self):
        with patch.object(upgrade.installer, 'files_for', return_value=self.full):
            return upgrade.payload(self.old)

    def test_all_managed_scripts_and_transport_assets_are_updated(self):
        files = self.payload()
        for path, source in upgrade.SCRIPT_FILES.items():
            self.assertEqual(files[path], self.full[source])
        for name in upgrade.TRANSPORT_FILES:
            self.assertEqual(files[upgrade.BASE+'/'+name], self.full['stack/'+name])
        rows = {row['name']:row for row in json.loads(files[upgrade.BASE+'/release.json'][0])['items']}
        self.assertEqual(rows['zigma-mode-backend']['version'], 'new')
        self.assertEqual(rows['zigma-free-service']['version'], 'new')
        self.assertEqual(rows['zigma-time-sync']['version'], 'new')
        self.assertEqual(rows['S98zigma-free'], self.original['items'][-1])
        for suffix in ('data/settings.json', 'device.conf', 'installer-firmware', 'local-mode.lock', 'vendor-init/S51hoslam'):
            self.assertNotIn(upgrade.BASE+'/'+suffix, files)
        self.assertNotIn('/root/etc/init.d/S98zigma-free', files)

    def test_full_without_telegram_preserves_optional_absence(self):
        self.old['items'] = [x for x in self.old['items'] if x['name'] != 'zigma-curl-arm']
        incoming = json.loads(self.full['stack/release.json'][0])
        incoming['items'] = [x for x in incoming['items'] if x['name'] != 'zigma-curl-arm']
        self.full['stack/release.json'] = (json.dumps(incoming).encode(), 0o600)
        for name in ('zigma-curl-arm',) + upgrade.TRANSPORT_FILES:
            del self.full['stack/'+name]
        with patch.object(upgrade.installer, 'files_for', return_value=self.full) as build:
            files = upgrade.payload(self.old)
        build.assert_called_once_with('full', telegram=False)
        for name in ('zigma-curl-arm',) + upgrade.TRANSPORT_FILES:
            self.assertNotIn(upgrade.BASE+'/'+name, files)
        self.assertIn(upgrade.BASE+'/free-zigma-agent-arm', files)
        self.assertIn(upgrade.BASE+'/zigma-free-service', files)
        expected = json.loads(files[upgrade.BASE+'/release.json'][0])
        self.assertNotIn('zigma-curl-arm', [x['name'] for x in expected['items']])
        upgrade.verify_inventory({'items':[dict(x, status='verified') for x in expected['items']]}, expected)

    def test_missing_required_binary_and_malformed_optional_inventory_rejected(self):
        for name, problem in (('free-zigma-agent-arm', 'missing'),
                              ('zigma-curl-arm', 'path'), ('zigma-curl-arm', 'kind'),
                              ('zigma-curl-arm', 'duplicate')):
            self.old = copy.deepcopy(self.original)
            row = next(x for x in self.old['items'] if x['name'] == name)
            if problem == 'missing': self.old['items'].remove(row)
            elif problem == 'duplicate': self.old['items'].append(row.copy())
            else: row[problem] = 'unexpected'
            with self.subTest(name=name, problem=problem), self.assertRaises(ValueError):
                self.payload()

    def test_missing_misidentified_or_duplicate_script_inventory_is_rejected(self):
        for problem in ('missing', 'path', 'kind', 'duplicate'):
            self.old = copy.deepcopy(self.original)
            row = next(x for x in self.old['items'] if x['name']=='zigma-mode-backend')
            if problem=='missing': self.old['items'].remove(row)
            elif problem=='duplicate': self.old['items'].append(row.copy())
            else: row[problem]='unexpected'
            with self.subTest(problem=problem), self.assertRaises(ValueError): self.payload()

    def test_corrupt_package_inventory_is_rejected(self):
        self.full['stack/zigma-mode-backend'] = (b'wrong', 0o700)
        with self.assertRaises(ValueError): self.payload()

    def test_script_verification_blocks_acceptance(self):
        self.payload()
        about = {'items':[dict(row, status='verified') for row in self.old['items']]}
        upgrade.verify_inventory(about, self.old)
        row = next(x for x in about['items'] if x['name']=='zigma-mode-backend')
        row['sha256']='old'
        with self.assertRaises(ValueError): upgrade.verify_inventory(about, self.old)
        row['sha256']=next(x['sha256'] for x in self.old['items'] if x['name']==row['name'])
        row['status']='modified'
        with self.assertRaises(ValueError): upgrade.verify_inventory(about, self.old)


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

    def test_legacy_missing_helpers_have_absent_backups(self):
        self.presence = 'absent'
        for name in ('activate', 'firmware-profile.sh'):
            with self.subTest(name=name):
                self.target = upgrade.BASE+'/'+name
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
                ('idle', None), ('firmware_gate', 'true'), ('timezone_payload', ({}, {})),
                ('read_file', (b'old', '600')),
                ('read_bytes', json.dumps(inventory).encode()),
                ('payload', {getattr(self, 'target', '/root/zigma-free/src/app.js'): (b'new', 0o644)}),
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
        (self.stage/'installer-version.txt').write_text('v1.1-rc20\n')
        (self.base/'installer-version.txt').write_text('manually edited old label\n')
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

    def timezone_files(self, existing=False):
        init = self.root/'root/etc/init.d'; init.mkdir(parents=True)
        self.nav = init/'S51hoslam'
        self.nav_before = b'#!/bin/sh\nexport HOSLAM_LASER_ANGLE=-2.386655\nexit 0\n'
        self.nav.write_bytes(self.nav_before); self.nav.chmod(0o750)
        self.tz = init/'S04zigma-timezone'
        tz_data = b'#!/bin/sh\nexit 0\n'
        rows = []
        for index, path, old, new, mode in [
            (0, self.tz, b'#!/bin/sh\n# previous\n' if existing else None, tz_data, '700'),
            (1, self.nav, self.nav_before, upgrade.navigation_timezone(self.nav_before), '750')]:
            if old is not None:
                path.write_bytes(old); path.chmod(int(mode, 8))
                (self.stage/f'{index}.old.gz').write_bytes(gzip.compress(old))
            else:
                (self.stage/f'{index}.old.gz').unlink(missing_ok=True)
            (self.stage/f'{index}.new.gz').write_bytes(gzip.compress(new))
            oldsha = hashlib.sha256(old).hexdigest() if old is not None else 'absent'
            rows.append(f'{index} {path} {mode if old is not None else "-"} {mode} {oldsha} {hashlib.sha256(new).hexdigest()}')
        (self.stage/'files.list').write_text('\n'.join(rows)+'\n')

    def managed_script_files(self, invalid=False):
        # Put data before the script: invalid shell must reject the whole batch,
        # not leave a partially updated web asset.
        self.backend = self.base/'zigma-mode-backend'
        self.backend_before = b'#!/bin/sh\n# old backend\nexit 0\n'
        self.backend.write_bytes(self.backend_before); self.backend.chmod(0o750)
        self.backend_after = (b'#!/bin/sh\nif then\n' if invalid else
                              b'#!/bin/sh\n# new backend\nexit 0\n')
        self.inventory = self.base/'release.json'
        old_inventory=b'{"version":"old"}'; new_inventory=b'{"version":"new"}'
        self.inventory.write_bytes(old_inventory); self.inventory.chmod(0o600)
        with (self.stage/'files.list').open('a') as out:
            for index,path,old,new,oldmode,newmode in [
                (1,self.backend,self.backend_before,self.backend_after,'750','700'),
                (2,self.inventory,old_inventory,new_inventory,'600','600')]:
                out.write(f'{index} {path} {oldmode} {newmode} {hashlib.sha256(old).hexdigest()} {hashlib.sha256(new).hexdigest()}\n')
                for direction,data in [('old',old),('new',new)]:
                    (self.stage/f'{index}.{direction}.gz').write_bytes(gzip.compress(data))

    def test_managed_script_and_inventory_accept(self):
        self.managed_script_files()
        process=self.start(); self.ready(process)
        subprocess.run(['sh',str(self.script),str(self.stage),'accept'],check=True)
        self.assertEqual(process.wait(timeout=5),0)
        self.assertEqual(self.backend.read_bytes(),self.backend_after)
        self.assertEqual(self.backend.stat().st_mode & 0o777,0o700)
        self.assertEqual(json.loads(self.inventory.read_text()),{'version':'new'})

    def test_managed_script_and_inventory_roll_back_on_disconnect(self):
        self.managed_script_files()
        self.start().wait(timeout=6)
        self.assertEqual(self.backend.read_bytes(),self.backend_before)
        self.assertEqual(self.backend.stat().st_mode & 0o777,0o750)
        self.assertEqual(json.loads(self.inventory.read_text()),{'version':'old'})
        self.assertEqual(self.target.read_bytes(),b'old')
        self.assertEqual((self.stage/'state').read_text().strip(),'rolled-back')

    def test_invalid_managed_script_rejects_before_any_write(self):
        self.managed_script_files(invalid=True)
        self.assertNotEqual(self.start().wait(timeout=5),0)
        self.assertEqual(self.backend.read_bytes(),self.backend_before)
        self.assertEqual(self.target.read_bytes(),b'old')
        self.assertEqual(json.loads(self.inventory.read_text()),{'version':'old'})

    def test_new_service_failure_restores_scripts_and_inventory(self):
        self.managed_script_files()
        service=self.base/'zigma-free-service'
        old=service.read_bytes();new=b'#!/bin/sh\nexit 1\n'
        with (self.stage/'files.list').open('a') as out:
            out.write(f'3 {service} 700 700 {hashlib.sha256(old).hexdigest()} {hashlib.sha256(new).hexdigest()}\n')
        for direction,data in [('old',old),('new',new)]:
            (self.stage/f'3.{direction}.gz').write_bytes(gzip.compress(data))
        self.assertNotEqual(self.start().wait(timeout=5),0)
        self.assertEqual(service.read_bytes(),old)
        self.assertEqual(self.backend.read_bytes(),self.backend_before)
        self.assertEqual(json.loads(self.inventory.read_text()),{'version':'old'})
        self.assertEqual((self.stage/'state').read_text().strip(),'rolled-back')

    def test_managed_script_external_edit_is_preserved(self):
        self.managed_script_files()
        process=self.start(); self.ready(process)
        self.backend.write_bytes(b'#!/bin/sh\n# external edit\n')
        (self.stage/'rollback.now').touch();process.wait(timeout=5)
        self.assertEqual(self.backend.read_bytes(),b'#!/bin/sh\n# external edit\n')
        self.assertEqual((self.stage/'state').read_text().strip(),'rollback-failed')

    def test_device_data_is_not_an_upgrade_target(self):
        self.target=self.base/'device.conf';self.target.write_bytes(b'old')
        (self.stage/'files.list').write_text(f'0 {self.target} 640 644 {self.oldsha} {self.newsha}\n')
        self.assertNotEqual(self.start().wait(timeout=5),0)
        self.assertEqual(self.target.read_bytes(),b'old')

    def test_timezone_accept_without_running_scripts(self):
        self.timezone_files()
        process = self.start(); self.ready(process)
        subprocess.run(['sh', str(self.script), str(self.stage), 'accept'], check=True)
        self.assertEqual(process.wait(timeout=5), 0)
        self.assertTrue(self.tz.is_file())
        self.assertEqual(self.nav.read_bytes(), upgrade.navigation_timezone(self.nav_before))
        self.assertEqual(self.nav.stat().st_mode & 0o777, 0o750)

    def test_timezone_disconnect_rolls_back_both_paths(self):
        self.timezone_files()
        self.start().wait(timeout=6)
        self.assertFalse(self.tz.exists())
        self.assertEqual(self.nav.read_bytes(), self.nav_before)
        self.assertEqual(self.nav.stat().st_mode & 0o777, 0o750)
        self.assertEqual((self.stage/'state').read_text().strip(), 'rolled-back')

    def test_timezone_existing_file_rollback(self):
        self.timezone_files(existing=True)
        process = self.start(); self.ready(process)
        (self.stage/'rollback.now').touch(); process.wait(timeout=5)
        self.assertEqual(self.tz.read_bytes(), b'#!/bin/sh\n# previous\n')
        self.assertEqual(self.nav.read_bytes(), self.nav_before)

    def test_timezone_partial_install_failure_rolls_back(self):
        self.timezone_files()
        service = self.base/'zigma-free-service'; marker = self.stage/'failed-once'
        service.write_text(f'#!/bin/sh\nif test ! -e "{marker}"; then touch "{marker}"; exit 1; fi\nexit 0\n')
        self.start().wait(timeout=5)
        self.assertFalse(self.tz.exists())
        self.assertEqual(self.nav.read_bytes(), self.nav_before)
        self.assertEqual((self.stage/'state').read_text().strip(), 'rolled-back')

    def test_timezone_external_edit_is_not_overwritten_on_rollback(self):
        self.timezone_files()
        process = self.start(); self.ready(process)
        self.nav.write_bytes(b'#!/bin/sh\n# external edit\n')
        (self.stage/'rollback.now').touch(); process.wait(timeout=5)
        self.assertEqual(self.nav.read_bytes(), b'#!/bin/sh\n# external edit\n')
        self.assertEqual((self.stage/'state').read_text().strip(), 'rollback-failed')

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

    def missing_legacy_helpers(self):
        rows=[]
        self.helpers=[]
        for index,name in enumerate(('activate','firmware-profile.sh')):
            path=self.base/name
            data=b'#!/bin/sh\n# fixture: must not execute\nexit 99\n'
            rows.append(f'{index} {path} - 700 absent {hashlib.sha256(data).hexdigest()}')
            (self.stage/f'{index}.new.gz').write_bytes(gzip.compress(data))
            (self.stage/f'{index}.old.gz').unlink(missing_ok=True)
            self.helpers.append(path)
        (self.stage/'files.list').write_text('\n'.join(rows)+'\n')

    def test_missing_legacy_helpers_are_added_without_execution(self):
        self.missing_legacy_helpers()
        process=self.start();self.ready(process)
        subprocess.run(['sh',str(self.script),str(self.stage),'accept'],check=True)
        self.assertEqual(process.wait(timeout=5),0)
        for path in self.helpers:
            self.assertIn(b'exit 99',path.read_bytes())
            self.assertEqual(path.stat().st_mode & 0o777,0o700)

    def test_missing_legacy_helpers_are_removed_on_rollback(self):
        self.missing_legacy_helpers()
        process=self.start();self.ready(process)
        (self.stage/'rollback.now').touch();process.wait(timeout=5)
        self.assertTrue(all(not p.exists() for p in self.helpers))
        self.assertEqual((self.stage/'state').read_text().strip(),'rolled-back')

    def test_legacy_helper_symlink_is_refused(self):
        self.missing_legacy_helpers()
        self.helpers[0].symlink_to(self.base/'nonexistent')
        self.assertNotEqual(self.start().wait(timeout=5),0)
        self.assertTrue(self.helpers[0].is_symlink())
        self.assertFalse(self.helpers[1].exists())

    def missing_license_directory(self):
        self.target=self.base/'licenses/transport.txt'
        (self.stage/'0.old.gz').unlink()
        (self.stage/'files.list').write_text(f'0 {self.target} - 644 absent {self.newsha}\n')

    def test_license_directory_created_and_accepted(self):
        self.missing_license_directory()
        process=self.start();self.ready(process)
        subprocess.run(['sh',str(self.script),str(self.stage),'accept'],check=True)
        self.assertEqual(process.wait(timeout=5),0)
        self.assertEqual(self.target.read_bytes(),b'new')
        self.assertEqual(self.target.parent.stat().st_mode & 0o777,0o755)

    def test_license_directory_removed_on_rollback(self):
        self.missing_license_directory()
        process=self.start();self.ready(process)
        (self.stage/'rollback.now').touch();process.wait(timeout=5)
        self.assertFalse(self.target.parent.exists())
        self.assertEqual((self.stage/'state').read_text().strip(),'rolled-back')

    def test_license_parent_symlink_reports_path_failure(self):
        self.missing_license_directory()
        other=self.root/'foreign';other.mkdir()
        self.target.parent.symlink_to(other,target_is_directory=True)
        self.assertNotEqual(self.start().wait(timeout=5),0)
        self.assertEqual(list(other.iterdir()),[])
        log=(self.stage/'log').read_text()
        self.assertIn('Preflight failed: target-path',log)
        self.assertIn('licenses/transport.txt',log)

    def test_rollback_preserves_concurrent_license_file(self):
        self.missing_license_directory()
        process=self.start();self.ready(process)
        external=self.target.parent/'user.txt';external.write_text('preserve')
        (self.stage/'rollback.now').touch();process.wait(timeout=5)
        self.assertEqual(external.read_text(),'preserve')
        self.assertFalse(self.target.exists())
        self.assertEqual((self.stage/'state').read_text().strip(),'rollback-failed')

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
        self.assertEqual((self.base/'installer-version.txt').read_text(),'v1.1-rc20\n')

    def test_edited_version_label_does_not_block_acceptance(self):
        process=self.start();self.ready(process)
        (self.base/'installer-version.txt').write_text('any manual text')
        subprocess.run(['sh',str(self.script),str(self.stage),'accept'],check=True)
        self.assertEqual(process.wait(timeout=5),0)
        self.assertEqual((self.base/'installer-version.txt').read_text(),'v1.1-rc20\n')

    def test_disconnect_rolls_back(self):
        process = self.start()
        process.wait(timeout=6)
        self.assertEqual(self.target.read_bytes(), b'old')
        self.assertEqual(self.target.stat().st_mode & 0o777, 0o640)
        self.assertEqual((self.base/'installer-version.txt').read_text(),'manually edited old label\n')
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
