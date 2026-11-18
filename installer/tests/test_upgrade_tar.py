import io
from pathlib import Path
import sys
import tarfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'computer'))
import upgrade

class TarBackups(unittest.TestCase):
    def archive(self, mode=0o751, kind=tarfile.REGTYPE, name='root/zigma-free/test', extra=False):
        output=io.BytesIO()
        with tarfile.open(fileobj=output,mode='w') as tar:
            info=tarfile.TarInfo(name);info.mode=mode;info.type=kind
            if kind==tarfile.REGTYPE:info.size=7
            tar.addfile(info,io.BytesIO(b'fixture') if kind==tarfile.REGTYPE else None)
            if extra:tar.addfile(tarfile.TarInfo('extra'))
        return output.getvalue()

    def test_bytes_permissions_and_no_stat(self):
        for mode in (0o600,0o751,0o4755,0o0):
            with patch.object(upgrade,'read_bytes',return_value=self.archive(mode)) as read:
                data,permissions=upgrade.read_file(None,'/root/zigma-free/test')
                self.assertEqual(data,b'fixture');self.assertEqual(int(permissions,8),mode)
                self.assertEqual(read.call_args.args[1],'tar -cf - -C / root/zigma-free/test')

    def test_rejects_link_wrong_path_and_extra_member(self):
        for data in (self.archive(kind=tarfile.SYMTYPE),self.archive(name='wrong'),self.archive(extra=True)):
            with patch.object(upgrade,'read_bytes',return_value=data),self.assertRaises(ValueError):
                upgrade.read_file(None,'/root/zigma-free/test')

    def test_truncated_member_refused(self):
        with patch.object(upgrade,'read_bytes',return_value=self.archive()[:514]),self.assertRaises((ValueError,tarfile.TarError)):
            upgrade.read_file(None,'/root/zigma-free/test')
