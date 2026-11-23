"""Exercise boot timezone overlays with a simulated bind mount; no robot actions."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]

class BootTimezone(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for folder in ('etc', 'root/etc', 'tmp', 'proc', 'bin'):
            (self.root/folder).mkdir(parents=True, exist_ok=True)
        self.zone = self.root/'root/etc/localtime'
        self.zone.symlink_to(REPO/'installer/robot/zoneinfo/Europe/Amsterdam')
        (self.root/'etc/profile').write_text('export KEEP_ME=yes\nexport TZ=CST-8\n')
        (self.root/'proc/mounts').write_text('')
        # Model the bind's same-inode semantics without mounting anything.
        mount = self.root/'bin/mount'
        mount.write_text('#!/bin/sh\nrm "$4" && ln "$3" "$4"\n')
        mount.chmod(0o700)
        self.env = dict(os.environ, PATH=str(self.root/'bin')+':'+os.environ['PATH'])
        self.script = self.root/'boot'
        self.script.write_text(self.translate((REPO/'installer/robot/init/S04zigma-timezone').read_text()))

    def translate(self, text):
        import re
        return re.sub(r'(?<![A-Za-z0-9_/])/(?:root|etc|tmp|proc)(?=/|\b)', lambda m:str(self.root)+m[0], text)

    def boot(self):
        return subprocess.run(['sh',str(self.script),'start'],env=self.env,capture_output=True,text=True)

    def test_profile_overrides_vendor_zone_preserves_settings_and_is_idempotent(self):
        self.assertEqual(self.boot().returncode,0)
        profile=self.root/'etc/profile'
        first=profile.read_bytes()
        self.assertEqual(self.boot().returncode,0)
        self.assertEqual(profile.read_bytes(),first)
        # Winter and summer dates: timezone change must not change Unix time.
        for epoch, expected in [(1768478400,'+0100'),(1784116800,'+0200')]:
            command=f'. "{profile}"; echo "$KEEP_ME"; date -d @{epoch} +%z; date -d @{epoch} +%s'
            out=subprocess.check_output(['sh','-c',command],env=self.env,text=True).splitlines()
            self.assertEqual(out,['yes',expected,str(epoch)])

    def test_missing_zone_leaves_profile_untouched(self):
        self.zone.unlink()
        before=(self.root/'etc/profile').read_bytes()
        self.assertEqual(self.boot().returncode,0)
        self.assertEqual((self.root/'etc/profile').read_bytes(),before)

    def test_foreign_mount_refused(self):
        (self.root/'proc/mounts').write_text('other '+str(self.root/'etc/profile')+' none rw 0 0\n')
        self.assertNotEqual(self.boot().returncode,0)

    def test_navigation_patch_is_idempotent_and_does_not_start_navigation(self):
        init=self.root/'root/etc/init.d';init.mkdir()
        nav=init/'S51hoslam'
        nav.write_text('#!/bin/sh\nprintf "%s\\n" "$TZ"\n')
        stage=self.root/'stage';stage.mkdir()
        support=self.root/'support';support.mkdir()
        for name in ('S01time','S03zigma-hostname','S04zigma-timezone','S96ntpsync','S97crond','zigma-time-sync'):
            (support/name).write_text('#!/bin/sh\nexit 0\n')
        (self.root/'root/bin').mkdir()
        code=self.translate((REPO/'installer/robot/configure-support.sh').read_text())
        driver=f'stage="{stage}"; support_dir="{support}"; install_hostname=test\nreplace() {{ cp "$1" "$2"; chmod "$3" "$2"; }}\n'+code+'\nconfigure_support\n'
        for _ in range(2):
            subprocess.run(['sh','-eu'],input=driver,text=True,check=True,capture_output=True)
        self.assertEqual(nav.read_text().count('# zigma-selected-timezone-v1'),1)
        self.assertEqual(subprocess.check_output(['sh',str(nav)],text=True).strip(),':'+str(self.zone))

if __name__=='__main__': unittest.main()
