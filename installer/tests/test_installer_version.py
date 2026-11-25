"""The installed version label is display-only, outside component integrity."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
HERE=Path(__file__).resolve().parents[1]/'computer'
sys.path.insert(0,str(HERE))
import installer
import upgrade

class InstallerVersion(unittest.TestCase):
    def test_package_label_and_source_fallback(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);computer=root/'computer';computer.mkdir()
            with patch.object(installer,'HERE',computer):
                self.assertEqual(installer.package_version(),'dev')
                (root/'VERSION').write_text('v1.1-rc20\n')
                self.assertEqual(installer.package_version(),'v1.1-rc20')
                (root/'VERSION').write_text('bad; command')
                with self.assertRaises(ValueError):installer.package_version()

    def test_full_install_writes_label_without_reading_existing_value(self):
        template=(HERE.parent/'robot/install.sh.in').read_text()
        start=template.index('if test "$profile" = full;then\n # Informational only:')
        end=template.index('\nfi',start)+3
        script=template[start:end].replace('@@INSTALLER_VERSION@@',"'v1.1-rc20'")
        with tempfile.TemporaryDirectory() as tmp:
            marker=Path(tmp)/'installer-version.txt'
            for old in (None,'manually changed!'):
                if old is not None:marker.write_text(old)
                subprocess.run(['sh','-c','set -eu; profile=full; store="$1"; '+script,'sh',tmp],check=True)
                self.assertEqual(marker.read_text(),'v1.1-rc20\n')
                self.assertEqual(marker.stat().st_mode & 0o777,0o644)
            marker.write_text('retained')
            subprocess.run(['sh','-c','set -eu; profile=ssh; store="$1"; '+script,'sh',tmp],check=True)
            self.assertEqual(marker.read_text(),'retained')

    def test_noop_upgrade_refreshes_only_label(self):
        from unittest.mock import Mock
        from contextlib import ExitStack
        with tempfile.TemporaryDirectory() as tmp, ExitStack() as stack:
            robot=Mock(); robot.command.return_value='boot'
            for name,value in [('select_connection',('192.0.2.1',Path(tmp)/'key',Mock())),('load_key',Mock()),
                               ('timezone_payload',({},{})),('firmware_gate','true'),('idle',None),
                               ('read_bytes',b'{"items":[]}'),('payload',{})]:
                stack.enter_context(patch.object(upgrade,name,return_value=value))
            stack.enter_context(patch.object(Path,'home',return_value=Path(tmp)))
            stack.enter_context(patch('wizard.Robot',return_value=robot))
            stack.enter_context(patch.object(installer,'package_version',return_value='v1.1-rc20'))
            upgrade.upgrade()
            commands=[x.args[0] for x in robot.command.call_args_list]
            command=next(x for x in commands if '.installer-version.XXXXXX' in x)
            base=Path(tmp)/'runtime';base.mkdir()
            (base/'installer-version.txt').write_text('manual value')
            subprocess.run(['sh','-c',command.replace(upgrade.BASE,str(base))],check=True)
            self.assertEqual((base/'installer-version.txt').read_text(),'v1.1-rc20\n')
            self.assertFalse(any('cat '+upgrade.BASE+'/installer-version.txt' in x for x in commands))
