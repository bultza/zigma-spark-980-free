"""Validate the portable timezone database and DST rules without a robot."""
import datetime
import hashlib
from pathlib import Path
import unittest
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parents[1] / 'robot/zoneinfo'

class Timezones(unittest.TestCase):
    def test_manifest_and_menu(self):
        names = ['UTC'] + [line.split()[2] for line in (ROOT/'zone.tab').read_text().splitlines()
                           if line and not line.startswith('#')]
        self.assertGreater(len(names), 400)
        for line in (ROOT/'SHA256SUMS').read_text().splitlines():
            digest, name = line.split('  ', 1)
            self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(), digest)
        for name in names:
            with self.subTest(zone=name), (ROOT/name).open('rb') as f:
                self.assertIsNotNone(ZoneInfo.from_file(f))

    def test_winter_and_summer_offsets(self):
        for name, hours in [('UTC', (0,0)), ('Europe/Madrid',(1,2)),
                            ('Europe/Berlin',(1,2)), ('America/New_York',(-5,-4))]:
            with (ROOT/name).open('rb') as f:
                zone = ZoneInfo.from_file(f)
            for month, expected in zip((1,7),hours):
                with self.subTest(zone=name,month=month):
                    instant=datetime.datetime(2026,month,15,12,tzinfo=datetime.timezone.utc)
                    local=instant.astimezone(zone)
                    self.assertEqual(local.utcoffset().total_seconds(),expected*3600)
                    self.assertEqual(local.timestamp(),instant.timestamp())
