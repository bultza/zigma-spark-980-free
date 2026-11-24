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

    def test_fan(self):
        for level,name in [(1,'minimum'),(2,'medium'),(3,'maximum')]:
            value=dict(working_status='AutoClean',fan=dict(level=level,percent=50))
            _,result=self.run_status(value)
            self.assertEqual(result['fan'],dict(level=level,name=name,percent=50,source='state_estimate',estimated=True))
            _,text=self.run_status(value,True)
            self.assertIn('Fan: '+name,text)
            self.assertIn('50%',text)
        for fan in [None,{},dict(level=4,percent=101),dict(level=1.5,percent=-1)]:
            _,result=self.run_status(dict(working_status='AutoClean',fan=fan))
            self.assertIsNone(result['fan']['level'])
            self.assertIsNone(result['fan']['percent'])

    def test_fan_state_estimate(self):
        for state in ['PileCharging','DirCharging','BackCharging','Standby','Hibernating','CleanDone','Malfunction','PlanningLocation']:
            _,result=self.run_status(dict(working_status=state,fan=dict(level=1,percent=20)))
            self.assertEqual(result['fan']['level'],0)
            self.assertEqual(result['fan']['percent'],0)
            self.assertTrue(result['fan']['estimated'])
            self.assertEqual(result['fan_setting']['level'],1)
            _,text=self.run_status(dict(working_status=state,fan=dict(level=1,percent=20)),True)
            self.assertIn('Fan: off (level 0/3)',text)
        for state in ['AutoClean','SpotClean','PlanningRect','SelectClean','DirectionControl']:
            _,result=self.run_status(dict(working_status=state,fan=dict(level=2,percent=50)))
            self.assertEqual(result['fan']['level'],2)
        _,result=self.run_status(dict(working_status='unknown',fan=dict(level=2,percent=50)))
        self.assertIsNone(result['fan']['level'])

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

    def test_map_position_fallback(self):
        geometry=dict(map_id='map-A',width=200,height=100,map_origin=[50,50])
        endpoint=dict(x=70,y=40,kind='recorded_path_endpoint',live=False)
        m=dict(geometry,available=True,path_endpoint=endpoint,path_changed_at=time.time()-100)
        def run(map_value, **status):
            (self.base/'map.json').write_text(json.dumps(map_value))
            return self.run_status(dict(working_status='Standby',**status))[1]['position']
        p=run(m)
        self.assertTrue(p['available']);self.assertFalse(p['fresh'])
        self.assertEqual(p['source'],'map_path');self.assertEqual(p['quality'],'recorded')
        self.assertEqual(p['x'],70);self.assertIsNone(p['heading']);self.assertIsNone(p['resolution_m'])
        self.assertGreaterEqual(p['age_s'],100)
        _,human=self.run_status(dict(working_status='Standby'),human=True)
        self.assertIn('recorded, map_path',human);self.assertIn('heading unavailable',human)
        self.assertNotIn('nan',human.lower())
        estimate=dict(endpoint,x=75,kind='recent_path_estimate',received_at=time.time())
        m['position_estimate']=estimate
        self.assertEqual(run(m)['quality'],'estimated')
        for stamp in (time.time()-60,time.time()+60,0):
            m['position_estimate']=dict(estimate,received_at=stamp)
            self.assertEqual(run(m)['quality'],'recorded')
        pose=dict(geometry,x=80,y=45,heading=0.5,resolution_m=0.05,
                  frame='map_grid',kind='navigation_pose_cache',cache_read_at=time.time())
        self.assertEqual(run(m,navigation_pose=pose)['source'],'navigation_memory')
        for bad in (dict(pose,map_id='other'),dict(pose,cache_read_at=time.time()-60)):
            self.assertEqual(run(m,navigation_pose=bad)['source'],'map_path')
        for badmap in (dict(m,available=False),dict(m,reset_pending=True),dict(m,map_origin=[]),
                       dict(m,path_endpoint=dict(endpoint,x=200)),dict(m,path_endpoint=None)):
            self.assertFalse(run(badmap)['available'])
        self.assertFalse(run(m,map_reset_pending=True)['available'])
        self.assertFalse(run(m,map_reset_pending={'phase':'waiting'})['available'])
        del m['path_changed_at'];self.assertIsNone(run(m)['age_s'])
        m['position']=dict(x=90,y=30,heading=99)
        self.assertEqual(run(m)['quality'],'reported');self.assertIsNone(run(m)['heading'])
        (self.base/'map.json').unlink()

    def test_invalid_arguments(self):
        r=subprocess.run([str(self.binary),'status','--xml'],capture_output=True)
        self.assertEqual(r.returncode,2)
        self.assertFalse(r.stdout)

if __name__ == '__main__':
    unittest.main()
