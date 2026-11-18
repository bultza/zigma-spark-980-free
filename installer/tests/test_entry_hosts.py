"""Entry-script hosts embedding, ordering and advisory failures."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('entry_installer', ROOT/'computer/installer.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)

class EntryHosts(unittest.TestCase):
    def script(self, profile):
        with patch.object(installer, 'files_for', return_value={}):
            script, _, _ = installer.build(profile, 'ecdsa-sha2-nistp256 fixture',
                                           'http://192.0.2.1:8000', 'Vacuum')
        subprocess.run(['sh', '-n'], input=script, check=True, capture_output=True)
        return script.decode()

    def test_same_helper_all_profiles_before_payload_and_completion(self):
        helper=(ROOT/'robot/vendor-hosts.sh').read_text().rstrip('\n')
        for profile in ('ssh','tools','full'):
            with self.subTest(profile=profile):
                script=self.script(profile)
                embedded=script.split("<<'ZIGMA_EMBEDDED_VENDOR_HOSTS'\n",1)[1].split('\nZIGMA_EMBEDDED_VENDOR_HOSTS\n',1)[0]
                self.assertEqual(embedded,helper)
                domains=embedded.split("<<'DOMAINS'\n")[1].split('\nDOMAINS')[0].splitlines()
                self.assertEqual(len(domains),16)
                self.assertIn('zigma.iot.grit-cloud.com',domains)
                call=script.index('sh "$stage/vendor-hosts.sh" 9>&-')
                self.assertLess(script.index('test "$action" != --check || exit 0'),call)
                self.assertLess(script.index('flock -n 9'),call)
                self.assertLess(script.index("fail 'Interrupted installation:"),call)
                self.assertLess(call,script.index('wget -q -T 30'))
                self.assertLess(call,script.index('tx=/root/.zigma-install/transaction-'))
                self.assertLess(call,script.index('>"$tx/session-result"'))

    def test_helper_failure_warns_and_continues(self):
        script=self.script('ssh')
        start=script.index('if cat >"$stage/vendor-hosts.sh"')
        end=script.index('\nif test "$profile" != ssh;then',start)
        block=script[start:end]
        helper=(ROOT/'robot/vendor-hosts.sh').read_text().rstrip('\n')
        for code in (0,1):
            with tempfile.TemporaryDirectory() as td:
                stage=Path(td)
                action=block.replace(helper,'exit '+str(code))
                shell='set -eu\nstage='+td+'\n'+action+'\necho completed >"$stage/completed"\n'
                result=subprocess.run(['sh'],input=shell,text=True,capture_output=True)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertTrue((stage/'completed').exists())
                self.assertEqual('WARNING:' in result.stderr,code!=0)
        result=subprocess.run(['sh'],input='set -eu\nstage=/nonexistent/zigma-hosts-test\n'+block+'\necho CONTINUED\n',text=True,capture_output=True)
        self.assertEqual(result.returncode,0)
        self.assertIn('WARNING:',result.stderr)
        self.assertIn('CONTINUED',result.stdout)

if __name__=='__main__':
    unittest.main()
