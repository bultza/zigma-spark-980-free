"""Exercise the installer extraction with a tar wrapper that rejects -z."""
import gzip
import hashlib
import io
from pathlib import Path
import shlex
import subprocess
import tarfile
import tempfile
import unittest

TEMPLATE = Path(__file__).resolve().parents[1] / 'robot/install.sh.in'


class BundleExtraction(unittest.TestCase):
    def extract(self, corrupt=False, wrong_manifest=False):
        content = b'fixture payload\n'
        digest = '0' * 64 if wrong_manifest else hashlib.sha256(content).hexdigest()
        archive = io.BytesIO()
        with tarfile.open(fileobj=archive, mode='w') as tar:
            for name, data in [('fixture', content), ('SHA256SUMS',
                    (digest + '  fixture\n').encode())]:
                entry = tarfile.TarInfo(name)
                entry.size = len(data)
                tar.addfile(entry, io.BytesIO(data))
        compressed = bytearray(gzip.compress(archive.getvalue()))
        if corrupt:
            compressed[-8] ^= 1  # Bad gzip CRC must stop before a transaction.
        template = TEMPLATE.read_text()
        block = template[template.index(' mkdir "$stage/payload"'):
                         template.index('\nfi\ntx=')]
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            (stage / 'bundle.tar.gz').write_bytes(compressed)
            script = '''set -eu
stage=''' + shlex.quote(directory) + '''
tar() {
 for arg in "$@";do
  case "$arg" in -*z*) echo 'tar -z unsupported' >&2; return 2;; esac
 done
 command tar "$@"
}
''' + block + '\nprintf "TRANSACTION_READY\\n"\n'
            result = subprocess.run(['sh'], input=script, text=True, capture_output=True)
            if not corrupt and not wrong_manifest:
                self.assertEqual((stage / 'payload/fixture').read_bytes(), content)
                self.assertFalse((stage / 'bundle.tar').exists())
            return result

    def test_tar_without_gzip_support(self):
        result = self.extract()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('TRANSACTION_READY', result.stdout)

    def test_corrupt_gzip_stops_before_transaction(self):
        result = self.extract(corrupt=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('TRANSACTION_READY', result.stdout)

    def test_wrong_manifest_stops_before_transaction(self):
        result = self.extract(wrong_manifest=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('TRANSACTION_READY', result.stdout)


if __name__ == '__main__':
    unittest.main()
