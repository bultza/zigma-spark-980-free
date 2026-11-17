"""Run generated firmware checks against synthetic SHA-256 inventories only."""
import copy
import importlib.util
import json
from pathlib import Path
import shlex
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('zigma_installer', ROOT/'computer/installer.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)
CATALOG = json.loads((ROOT/'computer/known-firmware.json').read_text())
BASE = 'ciccio-original-kernel20'
OVERLAY = 'ciccia-original-kernel23'
APP = '/root/app_server/wifi_app_server_grit'
NAV = '/root/hoslam/demo_rplidar'

class FirmwareChecks(unittest.TestCase):
    def run_checks(self, name=BASE, pair=None, changed=None, requested='full'):
        variant = CATALOG['variants'][name]
        hashes = dict(variant['files'])
        if pair:
            hashes.update(next(p['files'] for p in CATALOG['variants'][BASE]['native_pairs']
                               if p['name'] == pair))
        for path, value in (changed or {}).items():
            if value is None:
                hashes.pop(path, None)
            else:
                hashes[path] = value
        fake = ['sha256sum() {', ' case "$1" in']
        for path, value in hashes.items():
            fake.append(' ' + shlex.quote(path) + ') printf "%s  %s\\n" ' + shlex.quote(value) + ' "$1";;')
        fake += [' *) return 1;;', ' esac', '}']
        script = 'set -eu\n' + '\n'.join(fake) + '\n'
        script += 'layout=' + shlex.quote(variant['layout']) + '\n'
        script += 'profile=' + shlex.quote(requested) + '\nrequested_profile=$profile\n'
        script += installer.firmware_checks(CATALOG)
        script += '\nprintf "RESULT:%s:%s:%s:%s\\n" "$profile" "$firmware_bad" "${firmware_wifi_bin:-}" "${firmware_wifi_libs:-}"\n'
        subprocess.run(['sh', '-n'], input=script, text=True, check=True, capture_output=True)
        result = subprocess.run(['sh'], input=script, text=True, check=True, capture_output=True)
        outcome = next(line for line in result.stdout.splitlines() if line.startswith('RESULT:'))
        return outcome, result.stdout + result.stderr

    def test_original_bases_unchanged(self):
        for name in CATALOG['variants']:
            with self.subTest(name=name):
                result, _ = self.run_checks(name)
                self.assertTrue(result.startswith('RESULT:full:0:'))
        self.assertIn(':/usr/bin:/usr/lib:/lib', self.run_checks('direct-kernel16')[0])

    def test_puron_pair_all_requested_profiles(self):
        for requested in ['ssh', 'tools', 'full']:
            with self.subTest(profile=requested):
                result, output = self.run_checks(pair='puron', requested=requested)
                self.assertTrue(result.startswith('RESULT:' + requested + ':0:'))
                self.assertIn('Native firmware: puron\n', output)
                self.assertIn('Live LiDAR reading for Puron navigation is not yet supported', output)

    def test_puron_app_with_original_navigation(self):
        result, output = self.run_checks(pair='puron-app-original-navigation')
        self.assertTrue(result.startswith('RESULT:full:0:'))
        self.assertIn('Native firmware: puron-app-original-navigation', output)

    def test_other_bases_do_not_inherit_puron_pair(self):
        for name in ['direct-kernel16']:
            result, _ = self.run_checks(name, pair='puron')
            self.assertTrue(result.startswith('RESULT:ssh:1:'))

    def test_issue01a_overlay_full(self):
        variant = CATALOG['variants'][OVERLAY]
        self.assertEqual(variant['layout'], 'overlay')
        self.assertEqual(variant['files']['/dev/mtd2'],
                         '0e912343ba5e0ce27e302c3b98b0755540be6c503838d6bf8cca0633d2300d3c')
        self.assertEqual(variant['files']['/dev/mtd3'],
                         '84f570dfc1bcdf115e7e009025861a5c2b0a72be6d6315b28a1828e50fcc065f')
        self.assertEqual(variant['native_pairs'][0]['files'], {
            APP:'784fbcda505db72675bf01a82f5b02f6eca8cb313a7916176ed7bbf10d7dd912',
            NAV:'6b7b11091132e75d9787abd59a8ac4f758da3732cccf86dc5702e99bd772a5d8'})
        for requested in ['ssh', 'tools', 'full']:
            with self.subTest(profile=requested):
                result, output = self.run_checks(OVERLAY, pair='puron', requested=requested)
                self.assertEqual(result, 'RESULT:' + requested + ':0:/root/bin:/root/lib:/lib:/usr/lib')
                self.assertIn('Native firmware: puron', output)
                self.assertIn('Firmware recognized: ciccia-original-kernel23', output)

    def test_overlay_uncatalogued_mixed_pair(self):
        result, _ = self.run_checks(OVERLAY, pair='puron-app-original-navigation')
        self.assertTrue(result.startswith('RESULT:ssh:1:'))

    def test_bad_or_unreadable_native_files(self):
        for name in [BASE, OVERLAY]:
            for path in [APP, NAV]:
                for value in ['a'*64, None]:
                    with self.subTest(base=name, path=path, value=value):
                        result, output = self.run_checks(name, pair='puron', changed={path:value})
                        self.assertTrue(result.startswith('RESULT:ssh:1:'))
                        self.assertIn('Native firmware UNKNOWN', output)

    def test_no_independent_hash_allowlists(self):
        puron = next(p['files'] for p in CATALOG['variants'][BASE]['native_pairs'] if p['name']=='puron')
        result, _ = self.run_checks(changed={NAV:puron[NAV]})
        self.assertTrue(result.startswith('RESULT:ssh:1:'))

    def test_required_components_still_checked(self):
        for name in [BASE, OVERLAY]:
            for path in CATALOG['variants'][name]['files']:
                if path in [APP, NAV, '/root/yg/ygclient']:
                    continue
                with self.subTest(base=name, path=path):
                    result, _ = self.run_checks(name, pair='puron', changed={path:'a'*64})
                    self.assertTrue(result.startswith('RESULT:ssh:1:'))

    def test_ygclient_warning_policy_unchanged(self):
        result, output = self.run_checks(pair='puron', changed={'/root/yg/ygclient':'a'*64})
        self.assertTrue(result.startswith('RESULT:full:0:'))
        self.assertIn('Firmware WARNING: /root/yg/ygclient', output)
        result, _ = self.run_checks(pair='puron', changed={'/root/yg/ygclient':None})
        self.assertTrue(result.startswith('RESULT:ssh:1:'))

    def test_native_pair_validation(self):
        for mode in ['bad-hash', 'bad-path', 'duplicate-pair', 'duplicate-name', 'bad-name', 'bad-note']:
            catalog = copy.deepcopy(CATALOG)
            pairs = catalog['variants'][BASE]['native_pairs']
            if mode == 'bad-hash': pairs[0]['files'][APP] = 'a;echo injected'
            if mode == 'bad-path': pairs[0]['files']['/tmp/other'] = pairs[0]['files'].pop(APP)
            if mode == 'duplicate-pair': pairs.append(copy.deepcopy(pairs[0]))
            if mode == 'duplicate-name': pairs[1]['name'] = pairs[0]['name']
            if mode == 'bad-name': pairs[0]['name'] = 'puron;false'
            if mode == 'bad-note': pairs[0]['note'] = 'one\ntwo'
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                installer.firmware_checks(catalog)

if __name__ == '__main__':
    unittest.main()
