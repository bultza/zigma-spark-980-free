"""Offline timezone migration and exact vendor-script recognition."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

HERE = Path(__file__).resolve().parents[1]/'computer'
sys.path.insert(0, str(HERE))
import upgrade


class TimezoneMigration(unittest.TestCase):
    def test_idempotent_and_preserves_calibrations_and_bytes(self):
        before = b'#!/bin/sh\nexport HOSLAM_LASER_ANGLE=-2.386655\n# no final newline'
        after = upgrade.navigation_timezone(before)
        self.assertEqual(upgrade.navigation_timezone(after), after)
        self.assertEqual(after.replace(upgrade.TIMEZONE_BLOCK, b''), before)
        self.assertEqual(after.count(upgrade.TIMEZONE_MARKER), 1)

    def test_malformed_duplicate_or_misplaced_preamble_refused(self):
        for data in (b'#!/bin/bash\ntrue\n', b'#!/bin/sh\n'+upgrade.TIMEZONE_MARKER+b'\nwrong\n',
                     b'#!/bin/sh\ntrue\n'+upgrade.TIMEZONE_BLOCK,
                     b'#!/bin/sh\n'+upgrade.TIMEZONE_BLOCK*2):
            with self.subTest(data=data), self.assertRaises(ValueError):
                upgrade.navigation_timezone(data)

    def test_payload_preserves_navigation_mode_and_expected_bytes(self):
        before = b'#!/bin/sh\nexport HOSLAM_GYRO_KGTG=1.001960\n'
        # Source path is explicitly supplied, so the test works without package symlinks.
        root = HERE.parent/'robot'
        with patch.object(upgrade, 'read_file', return_value=(before, '750')), patch.object(upgrade.installer, 'ROBOT', root):
            files, expected = upgrade.timezone_payload(Mock())
        self.assertEqual(files[upgrade.NAV_INIT], (upgrade.navigation_timezone(before), 0o750))
        self.assertEqual(expected, {upgrade.NAV_INIT: before})
        self.assertEqual(set(files), {upgrade.NAV_INIT, upgrade.TIMEZONE_INIT})
        self.assertEqual(files[upgrade.TIMEZONE_INIT][0], (root/'init/S04zigma-timezone').read_bytes())

    def test_firmware_gate_recognizes_only_exact_preamble(self):
        script = upgrade.firmware_gate()
        subprocess.run(['sh', '-n'], input=script, text=True, check=True)
        definitions = script[:script.index('profile=full')]
        definitions += script[script.index('firmware_digest()'):script.index('verify_firmware_files()')]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)/'root'; nav = root/'etc/init.d/S51hoslam'; nav.parent.mkdir(parents=True)
            original = b'#!/bin/sh\nexport HOSLAM_LASER_ANGLE=-2.386655\n# untouched'
            code = definitions.replace('/root', str(root))
            literal = upgrade.TIMEZONE_BLOCK.decode().splitlines()[1]
            code = code.replace(literal.replace('/root', str(root)), literal)
            for data in (original, upgrade.navigation_timezone(original)):
                nav.write_bytes(data)
                out = subprocess.check_output(['sh', '-c', code+'\nfirmware_digest "'+str(nav)+'"'], text=True)
                self.assertEqual(out, hashlib.sha256(original).hexdigest())
            nav.write_bytes(upgrade.navigation_timezone(original)+b'\n# foreign change')
            out = subprocess.check_output(['sh', '-c', code+'\nfirmware_digest "'+str(nav)+'"'], text=True)
            self.assertNotEqual(out, hashlib.sha256(original).hexdigest())
            # Calibration normalization must operate on the stripped vendor script too.
            values = []
            for data in (original, upgrade.navigation_timezone(original)):
                nav.write_bytes(data)
                values.append(subprocess.check_output(['sh', '-c', code+'\nfirmware_calibration_digest "'+str(nav)+'"']))
            self.assertEqual(values[0], values[1])
