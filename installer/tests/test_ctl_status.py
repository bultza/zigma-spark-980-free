"""Exercise the real CLI against isolated observer snapshots, without robot IPC."""
import json
from pathlib import Path
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2] / 'src/robot'

class Status(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.base = Path(cls.temp.name)
        cls.snapshot = cls.base/'status.json'
        cls.binary = cls.base/'zigmactl'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
            '-I'+str(ROOT/'vendor/cjson'),
            '-DZIGMACTL_MAP_PATH="'+str(cls.base/'map.json')+'"',
            '-DZIGMACTL_WIRELESS_PATH="'+str(cls.base/'wireless')+'"',
            '-DZIGMACTL_CARRIER_PATH="'+str(cls.base/'carrier')+'"',
            '-DZIGMACTL_STATUS_PATH="'+str(cls.snapshot)+'"',
            str(ROOT/'src/zigmactl.c'), str(ROOT/'vendor/cjson/cJSON.c'),
            '-lm', '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_status(self, value=None, human=False):
        self.snapshot.unlink(missing_ok=True)
        if value is not None:
            self.snapshot.write_text(value if isinstance(value,str) else json.dumps(value))
        result = subprocess.run([str(self.binary), 'status']+([] if human else ['--json']), capture_output=True, text=True)
        return result, result.stdout if human else json.loads(result.stdout)

    def test_fresh_and_human(self):
        value=dict(working_status='PileCharging', battery_level=95, error='NoError', observed_at=time.time())
        r,s=self.run_status(value)
        self.assertEqual(r.returncode,0)
        self.assertTrue(s['fresh'])
        self.assertEqual(s['battery_level'],95)
        r,s=self.run_status(value,True)
        self.assertIn('Battery: 95%',s)
        self.assertIn('State: PileCharging',s)

    def test_stale_and_future(self):
        for delta,quality in [(-120,'stale'),(120,'clock_mismatch')]:
            r,s=self.run_status(dict(working_status='Standby',observed_at=time.time()+delta))
            self.assertEqual(r.returncode,0)
            self.assertFalse(s['fresh'])
            self.assertEqual(s['telemetry_state'],quality)
            self.assertIsNone(s['battery_level'])

    def test_cached_does_not_claim_fresh(self):
        r,s=self.run_status(dict(working_status='Hibernating',observed_at=0,
            cache_read_at=time.time(),local_link_received_at=time.time()))
        self.assertFalse(s['fresh'])
        self.assertTrue(s['cache_available'])
        self.assertTrue(s['local_link_active'])
        self.assertEqual(s['telemetry_state'],'cached')

    def test_missing_invalid_and_oversize(self):
        for value in [None,'broken','{}','[]','x'*65537,'{"working_status":"Standby"} junk']:
            r,s=self.run_status(value)
            self.assertEqual(r.returncode,1)
            self.assertFalse(s['available'])
            self.assertFalse(s['fresh'])

    def test_wifi_and_statistics(self):
        (self.base/'carrier').write_text('1')
        (self.base/'wireless').write_text('wlan0: 0000 70. -48. 0.\n')
        value=dict(working_status='Standby', clean_time_s=125, clean_area_m2=12.5)
        r,s=self.run_status(value)
        self.assertTrue(s['wifi']['connected'])
        self.assertEqual(s['wifi']['signal_dbm'],-48)
        r,text=self.run_status(value,True)
        self.assertIn('2 min 5 s',text)
        self.assertIn('12.50 m2',text)
        (self.base/'carrier').write_text('0')
        r,s=self.run_status(value)
        self.assertFalse(s['wifi']['connected'])
        self.assertIsNone(s['wifi']['signal_dbm'])

    def test_position_validation(self):
        geometry=dict(map_id='map-A',width=200,height=100,map_origin=[50,50])
        (self.base/'map.json').write_text(json.dumps(geometry))
        pose=dict(geometry, x=70,y=40,heading=0.5,resolution_m=0.05,
                  frame='map_grid',kind='navigation_pose_cache',cache_read_at=time.time())
        value=dict(working_status='Standby',navigation_pose=pose)
        r,s=self.run_status(value)
        self.assertTrue(s['position']['available'])
        self.assertFalse(s['position']['fresh'])
        self.assertEqual(s['position']['x'],70)
        for key,bad in [('map_id','map-B'),('cache_read_at',time.time()-60),('x',201),('map_origin',[0,0]),('resolution_m',0),('heading',99)]:
            with self.subTest(key=key):
                invalid=dict(pose);invalid[key]=bad
                r,s=self.run_status(dict(value,navigation_pose=invalid))
                self.assertFalse(s['position']['available'])
                self.assertIsNone(s['position']['x'])
        (self.base/'map.json').unlink()
        r,s=self.run_status(value)
        self.assertFalse(s['position']['available'])

    def test_invalid_arguments(self):
        r=subprocess.run([str(self.binary),'status','--xml'],capture_output=True)
        self.assertEqual(r.returncode,2)
        self.assertFalse(r.stdout)

if __name__ == '__main__':
    unittest.main()
