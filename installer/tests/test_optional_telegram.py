"""Exercise optional transport argument selection from the production service."""
from pathlib import Path
import subprocess
import tempfile
import unittest
SERVICE=Path(__file__).resolve().parents[1]/'robot/src/zigma-free-service'
class OptionalTelegram(unittest.TestCase):
    def test_complete_absent_and_partial_transports(self):
        text=SERVICE.read_text()
        block=text[text.index('  # Telegram transport is optional'):text.index('  nohup nice -n 10 "$base/free-zigma-agent-arm"')]
        with tempfile.TemporaryDirectory(prefix='optional telegram ') as temp:
            base=Path(temp);curl=base/'zigma-curl-arm';ca=base/'telegram-ca.pem'
            for executable,certificate in ((False,False),(True,False),(False,True),(True,True)):
                with self.subTest(executable=executable,certificate=certificate):
                    curl.unlink(missing_ok=True);ca.unlink(missing_ok=True)
                    if executable:curl.write_text('#!/bin/sh\nexit 0\n');curl.chmod(0o700)
                    if certificate:ca.write_text('fixture')
                    script='set -eu\nbase=$1\n'+block+'\nprintf "%s\\n" "$#" "$@"\n'
                    result=subprocess.check_output(['sh','-c',script,'test',str(base)],text=True).splitlines()
                    expected=['4','--telegram-curl',str(curl),'--telegram-ca',str(ca)] if executable and certificate else ['0']
                    self.assertEqual(result,expected)
