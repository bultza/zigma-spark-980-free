"""Run generated firmware checks against synthetic SHA-256 inventories only."""
import copy
import importlib.util
import json
from pathlib import Path
import shlex
import subprocess
import unittest
import tempfile
import hashlib

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
    def run_checks(self, name=BASE, pair=None, changed=None, requested='full', catalog=None):
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
        script += installer.firmware_checks(CATALOG if catalog is None else catalog)
        script += '\nprintf "RESULT:%s:%s:%s:%s\\n" "$profile" "$firmware_bad" "${firmware_wifi_bin:-}" "${firmware_wifi_libs:-}"\n'
        subprocess.run(['sh', '-n'], input=script, text=True, check=True, capture_output=True)
        result = subprocess.run(['sh'], input=script, text=True, check=True, capture_output=True)
        outcome = next(line for line in result.stdout.splitlines() if line.startswith('RESULT:'))
        return outcome, result.stdout + result.stderr

    def test_issue10_full_and_strict_components(self):
        base = 'direct-kernel16'
        cp = CATALOG['variants'][base]['component_profiles'][0]
        for requested in ('ssh', 'tools', 'full'):
            result, output = self.run_checks(base, changed=cp['files'], requested=requested)
            self.assertEqual(result, 'RESULT:' + requested + ':0:/usr/bin:/usr/lib:/lib')
            self.assertIn('Firmware recognized: ciccio-issue10-kernel16', output)
        for path in cp['files']:
            for value in (None, 'a'*64):
                changed = dict(cp['files']); changed[path] = value
                with self.subTest(path=path, value=value):
                    self.assertTrue(self.run_checks(base, changed=changed)[0].startswith('RESULT:ssh:1:'))
        # A known alternate startup script does not authorize a Puron pair.
        changed = dict(cp['files'])
        changed.update(CATALOG['variants'][BASE]['native_pairs'][0]['files'])
        self.assertTrue(self.run_checks(base, changed=changed)[0].startswith('RESULT:ssh:1:'))

    def test_issue10_extension_preserves_other_decisions(self):
        old = copy.deepcopy(CATALOG)
        old['variants']['direct-kernel16']['component_profiles'] = [
            p for p in old['variants']['direct-kernel16']['component_profiles']
            if p['name'] != 'ciccio-issue10-kernel16']
        old['variants']['direct-kernel16'].pop('component_selector_files')
        for base, variant in old['variants'].items():
            cases = [{}] + [p['files'] for p in variant.get('native_pairs', [])]
            cases += [p['files'] for p in variant.get('component_profiles', [])]
            for case in cases:
                for mutation in ({}, {APP: 'a'*64}, {NAV: None},
                                 {'/root/etc/init.d/S70yg': 'a'*64}):
                    changed = dict(case); changed.update(mutation)
                    for requested in ('ssh', 'tools', 'full'):
                        with self.subTest(base=base, mutation=mutation, requested=requested):
                            self.assertEqual(
                                self.run_checks(base, changed=changed, requested=requested, catalog=old)[0],
                                self.run_checks(base, changed=changed, requested=requested)[0])
        # Do not extend the exception to other bases where it was not accepted.
        script = CATALOG['variants']['direct-kernel16']['component_profiles'][0]['files']['/root/etc/init.d/S70yg']
        for base in old['variants']:
            if base != 'direct-kernel16':
                changed = {'/root/etc/init.d/S70yg': script}
                self.assertEqual(self.run_checks(base, changed=changed, catalog=old)[0],
                                 self.run_checks(base, changed=changed)[0])

    def test_issue10_ciccia_exact_components(self):
        base = 'ciccia-hybrid-01b-kernel23'
        cp = next(p for p in CATALOG['variants'][base]['component_profiles']
                  if p['name'] == 'ciccia-issue10-kernel23')
        for requested in ('ssh', 'tools', 'full'):
            result, output = self.run_checks(base, changed=cp['files'], requested=requested)
            self.assertEqual(result, 'RESULT:' + requested + ':0:/usr/bin:/usr/lib:/lib')
            self.assertIn('Firmware recognized: ciccia-issue10-kernel23', output)
        for path in cp['files']:
            for value in (None, 'a'*64):
                changed = dict(cp['files']); changed[path] = value
                with self.subTest(path=path, value=value):
                    self.assertTrue(self.run_checks(base, changed=changed)[0].startswith('RESULT:ssh:1:'))
        changed = dict(cp['files'])
        changed['/root/modules/8189fs.ko'] = CATALOG['variants']['direct-kernel16']['files']['/root/modules/8189fs.ko']
        self.assertTrue(self.run_checks(base, changed=changed)[0].startswith('RESULT:ssh:1:'))

    def test_issue10_ciccia_preserves_existing_profiles(self):
        old = copy.deepcopy(CATALOG)
        variant = old['variants']['ciccia-hybrid-01b-kernel23']
        variant['component_profiles'] = [p for p in variant['component_profiles']
                                         if p['name'] != 'ciccia-issue10-kernel23']
        variant.pop('component_selector_files')
        for base, variant in old['variants'].items():
            cases = [{}] + [p['files'] for p in variant.get('native_pairs', [])]
            cases += [p['files'] for p in variant.get('component_profiles', [])]
            for case in cases:
                for mutation in ({}, {APP: 'a'*64}, {NAV: None},
                                 {'/root/etc/init.d/S70yg': 'a'*64}):
                    changed = dict(case); changed.update(mutation)
                    with self.subTest(base=base, mutation=mutation):
                        self.assertEqual(self.run_checks(base, changed=changed, catalog=old)[0],
                                         self.run_checks(base, changed=changed)[0])

    def test_issue10_selector_validation(self):
        for value in ('bad', ['/tmp/unreviewed'], [APP],
                      ['/root/etc/init.d/S70yg']*2, [None]):
            catalog = copy.deepcopy(CATALOG)
            catalog['variants']['direct-kernel16']['component_selector_files'] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                installer.firmware_checks(catalog)
        catalog = copy.deepcopy(CATALOG)
        variant = catalog['variants']['direct-kernel16']
        variant['component_profiles'][0]['files']['/root/etc/init.d/S70yg'] = variant['files']['/root/etc/init.d/S70yg']
        with self.assertRaises(ValueError):
            installer.firmware_checks(catalog)

    def test_robojet_original_exact_components(self):
        base = 'direct-kernel16'
        cp = next(p for p in CATALOG['variants'][base]['component_profiles']
                  if p['name'] == 'robojet-issue08-kernel16')
        for requested in ('ssh', 'tools', 'full'):
            result, output = self.run_checks(base, changed=cp['files'], requested=requested)
            self.assertEqual(result, 'RESULT:' + requested + ':0:/usr/bin:/usr/lib:/lib')
            self.assertIn('Firmware recognized: robojet-issue08-kernel16', output)
        for path in cp['files']:
            for value in (None, 'a'*64):
                changed = dict(cp['files']); changed[path] = value
                with self.subTest(path=path, value=value):
                    self.assertTrue(self.run_checks(base, changed=changed)[0].startswith('RESULT:ssh:1:'))
        adapted = CATALOG['variants']['ciccia-hybrid-01b-kernel23']['component_profiles'][0]['files']
        for path in ('/root/modules/8189fs.ko', '/root/etc/init.d/S51hoslam', APP, NAV):
            changed = dict(cp['files'])
            changed[path] = adapted[path] if path not in (APP, NAV) else CATALOG['variants'][base]['files'][path]
            with self.subTest(mixed_component=path):
                self.assertTrue(self.run_checks(base, changed=changed)[0].startswith('RESULT:ssh:1:'))

    def test_robojet_original_preserves_existing_profiles(self):
        old = copy.deepcopy(CATALOG)
        old['variants']['direct-kernel16']['component_profiles'] = [
            p for p in old['variants']['direct-kernel16']['component_profiles']
            if p['name'] != 'robojet-issue08-kernel16']
        for base, variant in old['variants'].items():
            cases = [{}] + [p['files'] for p in variant.get('native_pairs', [])]
            cases += [p['files'] for p in variant.get('component_profiles', [])]
            for case in cases:
                for mutation in ({}, {APP: 'a'*64}, {NAV: None}):
                    changed = dict(case); changed.update(mutation)
                    with self.subTest(base=base, mutation=mutation):
                        self.assertEqual(self.run_checks(base, changed=changed, catalog=old)[0],
                                         self.run_checks(base, changed=changed)[0])

    def test_robojet_ciccia_exact_component_profile(self):
        base='ciccia-hybrid-01b-kernel23'
        component=CATALOG['variants'][base]['component_profiles'][0]
        files=component['files']
        result,output=self.run_checks(base,changed=files)
        self.assertEqual(result,'RESULT:full:0:/usr/bin:/usr/lib:/lib')
        self.assertIn('ciccia-robojet-issue08-kernel23',output)
        for path in files:
            with self.subTest(path=path):
                changed=dict(files);changed[path]='a'*64
                self.assertTrue(self.run_checks(base,changed=changed)[0].startswith('RESULT:ssh:1:'))
        for path in (APP,NAV):
            changed=dict(files);changed[path]=CATALOG['variants'][base]['files'][path]
            self.assertTrue(self.run_checks(base,changed=changed)[0].startswith('RESULT:ssh:1:'))

    def test_original_bases_unchanged(self):
        for name in CATALOG['variants']:
            with self.subTest(name=name):
                result, _ = self.run_checks(name)
                self.assertTrue(result.startswith('RESULT:full:0:'))
        self.assertIn(':/usr/bin:/usr/lib:/lib', self.run_checks('direct-kernel16')[0])

    def test_issue01b_hybrid_exact_components(self):
        name = 'ciccia-hybrid-01b-kernel23'
        original = CATALOG['variants']['direct-kernel16']
        hybrid = CATALOG['variants'][name]
        self.assertEqual({p for p in original['files']
                          if original['files'][p] != hybrid['files'][p]},
                         {'/dev/mtd1', '/dev/mtd2', '/root/modules/8189fs.ko'})
        self.assertEqual(hybrid['layout'], 'direct')
        for requested in ['ssh', 'tools', 'full']:
            result, output = self.run_checks(name, requested=requested)
            self.assertEqual(result, 'RESULT:' + requested + ':0:/usr/bin:/usr/lib:/lib')
            self.assertIn('Firmware recognized: ' + name, output)
        for path in hybrid['files']:
            if path == '/root/yg/ygclient':
                continue  # Existing readable-client warning policy is unchanged.
            with self.subTest(path=path):
                result, _ = self.run_checks(name, changed={path:'a'*64})
                self.assertTrue(result.startswith('RESULT:ssh:1:'))
        result, _ = self.run_checks(name, pair='puron')
        self.assertTrue(result.startswith('RESULT:ssh:1:'))

    def test_issue03_hybrid_exact_components(self):
        name = 'ciccia-hybrid-03-kernel23'
        hybrid = CATALOG['variants'][name]
        self.assertEqual(hybrid['layout'], 'direct')
        self.assertEqual(hybrid['files']['/dev/mtd3'], CATALOG['variants'][BASE]['files']['/dev/mtd3'])
        self.assertEqual(hybrid['files']['/dev/mtd2'], CATALOG['variants'][OVERLAY]['files']['/dev/mtd2'])
        self.assertEqual(hybrid['files'][NAV], CATALOG['variants'][BASE]['native_pairs'][0]['files'][NAV])
        self.assertTrue(self.run_checks(name)[0].startswith('RESULT:full:0:/root/bin:'))
        for path in hybrid['files']:
            if path == '/root/yg/ygclient':
                continue
            with self.subTest(path=path):
                self.assertTrue(self.run_checks(name, changed={path: 'a'*64})[0].startswith('RESULT:ssh:1:'))

    def test_puron_pair_all_requested_profiles(self):
        for requested in ['ssh', 'tools', 'full']:
            with self.subTest(profile=requested):
                result, output = self.run_checks(pair='puron', requested=requested)
                self.assertTrue(result.startswith('RESULT:' + requested + ':0:'))
                self.assertIn('Native firmware: puron\n', output)
                self.assertIn('Puron saved maps and live LiDAR are supported', output)

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

    def test_issue04_full_component_profile(self):
        cp = CATALOG['variants'][OVERLAY]['component_profiles'][0]
        for requested in ('ssh', 'tools', 'full'):
            result, output = self.run_checks(OVERLAY, changed=cp['files'], requested=requested)
            self.assertEqual(result, 'RESULT:' + requested + ':0:/root/bin:/root/lib:/lib:/usr/lib')
            self.assertIn('Firmware recognized: ciccia-issue04-original-navigation', output)
        for path in cp['files']:
            for value in ('a'*64, None):
                altered = dict(cp['files']); altered[path] = value
                with self.subTest(path=path, value=value):
                    result, _ = self.run_checks(OVERLAY, changed=altered)
                    self.assertTrue(result.startswith('RESULT:ssh:1:'), result)

    def test_issue04_not_a_cross_base_or_navigation_allowlist(self):
        cp = CATALOG['variants'][OVERLAY]['component_profiles'][0]
        pair = {p: cp['files'][p] for p in (APP, NAV)}
        for base in CATALOG['variants']:
            if base == OVERLAY:
                continue
            result, _ = self.run_checks(base, changed=pair)
            self.assertTrue(result.startswith('RESULT:ssh:1:'))
        for nav in ('1b2751efd2a4d5afc208fce099401d90c581f0cec41cfe440e9a39d519ece664',
                    CATALOG['variants'][BASE]['native_pairs'][0]['files'][NAV]):
            altered = dict(cp['files']); altered[NAV] = nav
            self.assertTrue(self.run_checks(OVERLAY, changed=altered)[0].startswith('RESULT:ssh:1:'))
        # An Issue04 app pair on otherwise original Ciccia must not bypass its
        # distinct daemon and startup checks.
        self.assertTrue(self.run_checks(OVERLAY, changed=pair)[0].startswith('RESULT:ssh:1:'))

    def test_russian_original_exact_components(self):
        cp = CATALOG['variants'][OVERLAY]['component_profiles'][1]
        original = dict(cp['files']); original['/root/modules/8189fs.ko'] = None
        for requested in ('ssh', 'tools', 'full'):
            result, output = self.run_checks(OVERLAY, changed=original, requested=requested)
            self.assertTrue(result.startswith('RESULT:' + requested + ':0:'))
            self.assertIn('Firmware recognized: issue04-russian-original', output)
        for path in cp['files']:
            for value in (None, 'b'*64):
                changed = dict(original); changed[path] = value
                with self.subTest(path=path, value=value):
                    self.assertTrue(self.run_checks(OVERLAY, changed=changed)[0].startswith('RESULT:ssh:1:'))
        # Native pair alone cannot authorize a foreign base or Ciccia Wi-Fi.
        for base in CATALOG['variants']:
            self.assertTrue(self.run_checks(base, changed={APP:cp['files'][APP], NAV:cp['files'][NAV]})[0].startswith('RESULT:ssh:1:'))

    def test_component_module_replacements_are_narrow(self):
        for mode in ('non-module', 'missing-new', 'kept-old', 'not-a-dict'):
            c = copy.deepcopy(CATALOG); cp = c['variants'][OVERLAY]['component_profiles'][1]
            if mode == 'non-module': cp['replace_module_checks'] = {'/root/app_server/daemon':'/root/firmware/arobot/modules/8723ds.ko'}
            if mode == 'missing-new': cp['files'].pop('/root/firmware/arobot/modules/8723ds.ko')
            if mode == 'kept-old': cp['files']['/root/modules/8189fs.ko'] = 'a'*64
            if mode == 'not-a-dict': cp['replace_module_checks'] = []
            with self.subTest(mode=mode), self.assertRaises(ValueError): installer.firmware_checks(c)

    def test_navigation_calibration_digest_only_ignores_numeric_exports(self):
        script = installer.firmware_checks(CATALOG)
        helper = script[script.index('firmware_calibration_digest() {'):script.index('verify_firmware_files() {')]
        with tempfile.TemporaryDirectory() as d:
            path = Path(d)/'S51hoslam'
            def digest(text):
                path.write_text(text)
                return subprocess.check_output(['sh', '-c', helper+'\nfirmware_calibration_digest "$1"', 'test', str(path)], text=True)
            template = '#!/bin/sh\n\texport HOSLAM_LASER_ANGLE=CALIBRATION\nexport HOSLAM_LIDAR_MODEL=camsense\n'
            expected = hashlib.sha256(template.encode()).hexdigest()
            for value in ('-2.870378', '+1.25', '0', '43.000000'):
                self.assertEqual(digest(template.replace('CALIBRATION', value)), expected)
            for value in ('1; reboot', '$(id)', '1 # comment', 'nan', ''):
                self.assertNotEqual(digest(template.replace('CALIBRATION', value)), expected)
            self.assertNotEqual(digest(template.replace('CALIBRATION', '1').replace('camsense', 'other')), expected)

    def test_legacy_decisions_identical_without_issue04_extension(self):
        legacy = copy.deepcopy(CATALOG)
        for variant in legacy['variants'].values():
            variant.pop('component_profiles', None)
        for name, variant in legacy['variants'].items():
            pairs = [{}] + [p['files'] for p in variant.get('native_pairs', [])]
            for pair in pairs:
                for requested in ('ssh', 'tools', 'full'):
                    for mutation in ({}, {APP:'a'*64}, {NAV:None}, {'/root/bin/hold380':'b'*64}):
                        changed = dict(pair); changed.update(mutation)
                        with self.subTest(name=name, pair=pair, requested=requested, mutation=mutation):
                            old = self.run_checks(name, changed=changed, requested=requested, catalog=legacy)[0]
                            new = self.run_checks(name, changed=changed, requested=requested)[0]
                            self.assertEqual(old, new)

    def test_component_profile_validation(self):
        for mode in ('missing-file', 'wrong-base', 'bad-path', 'bad-name', 'bad-hash', 'duplicate', 'overlap-native'):
            c = copy.deepcopy(CATALOG)
            ps = c['variants'][OVERLAY]['component_profiles']; p = ps[0]
            if mode == 'missing-file': p['files'].pop('/root/app_server/daemon')
            if mode == 'wrong-base': p['files']['/dev/mtd2'] = 'a'*64
            if mode == 'bad-path': p['files']['/tmp/a;false'] = 'a'*64
            if mode == 'bad-name': p['name'] = 'bad;false'
            if mode == 'bad-hash': p['files'][APP] = 'bad'
            if mode == 'duplicate': ps.append(copy.deepcopy(p))
            if mode == 'overlap-native': p['files'][APP] = c['variants'][OVERLAY]['files'][APP]
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                installer.firmware_checks(c)

if __name__ == '__main__':
    unittest.main()
