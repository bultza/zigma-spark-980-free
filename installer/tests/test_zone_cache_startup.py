"""Exercise the actual service's one-shot configuration-read block offline."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

SERVICE = Path(__file__).resolve().parents[1]/'robot/src/zigma-free-service'
ISSUE04 = '8084000b686d071f8f0590448087ff88d71deee3e55e772c86d41f27d704516c'

class ZoneCacheStartup(unittest.TestCase):
    def check_runs(self, digest, identities, fail=False):
        text = SERVICE.read_text()
        block = text[text.index('  # One configuration read'):text.index('  # nice/exec')]
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); tool=root/'zigma-zones-arm'
            tool.write_text('#!/bin/sh\necho "$*" >> "$ram/calls"\nexit '+('1' if fail else '0')+'\n')
            tool.chmod(0o700)
            env=dict(os.environ, base=directory, ram=directory, DIGEST=digest)
            script='set -eu\nexport base ram\napp_pid=123\nsha256sum() { printf "%s  executable\\n" "$DIGEST"; }\n'
            for identity in identities:
                script+='native_key='+identity+'\n'+block+'\n'
            subprocess.run(['sh'],input=script,text=True,env=env,check=True,capture_output=True)
            calls=(root/'calls').read_text().splitlines() if (root/'calls').exists() else []
            return calls,(root/'zones-request.identity').read_text().strip() if (root/'zones-request.identity').exists() else None

    def test_only_once_until_native_identity_changes(self):
        calls,marker=self.check_runs(ISSUE04,['1:10:2:20','1:10:2:20','3:30:4:40'])
        self.assertEqual(calls,['--refresh-cache','--refresh-cache'])
        self.assertEqual(marker,'3:30:4:40')

    def test_other_profiles_send_no_configuration_request(self):
        for digest in ('4cf0709919f0e894a5d3ed1c60ca9d6bf11d7c47120077e23f6c13902c5d9a9a',
                       '784fbcda505db72675bf01a82f5b02f6eca8cb313a7916176ed7bbf10d7dd912'):
            calls,marker=self.check_runs(digest,['1:10:2:20','1:10:2:20'])
            self.assertEqual(calls,[]);self.assertEqual(marker,'1:10:2:20')

    def test_failure_does_not_publish_success_marker(self):
        calls,marker=self.check_runs(ISSUE04,['1:10:2:20'],fail=True)
        self.assertEqual(calls,['--refresh-cache']);self.assertIsNone(marker)

    def test_native_hash_gate_accepts_both_issue04_combinations(self):
        text = SERVICE.read_text()
        block = text[text.index('  # Recognize reviewed app-server'):text.index('  native_ids', text.index('  # Recognize reviewed app-server'))]
        russian='1b2751efd2a4d5afc208fce099401d90c581f0cec41cfe440e9a39d519ece664'
        ciccia='b10fc7b48d1bdb990a2c37b82f7d45fa4fd7358f428f42d58ae885aca2970255'
        for nav in (russian, ciccia, 'unknown'):
            script='set -eu\napp_pid=1\nnav_pid=2\nsha256sum() { case "$1" in /proc/1/exe) echo '+ISSUE04+';; *) echo '+nav+';; esac; }\n'+block
            result=subprocess.run(['sh'],input=script,text=True,capture_output=True)
            self.assertEqual(result.returncode, 1 if nav=='unknown' else 0)
