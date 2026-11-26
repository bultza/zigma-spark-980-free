#!/usr/bin/env python3
"""Host-only process tests. All command/mount syscalls are shims; no robot."""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import threading

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='zigma-wake-test-') as folder:
    d = Path(folder)
    audio, status, log, lock = (d / n for n in ('madplay', 'status.json', 'commands', 'lock'))
    binary = d / 'ctl'
    definitions = {'WAKE_MADPLAY': audio, 'ZIGMA_WAKE_STATUS_PATH': status,
                   'WAKE_TEST_LOG': log, 'ZIGMA_WAKE_LOCK': lock}
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-I'+str(ROOT/'vendor/cjson'),
                    *[f'-D{k}="{v}"' for k,v in definitions.items()],
                    str(ROOT/'tests/test_wake_process.c'), str(ROOT/'vendor/cjson/cJSON.c'),
                    '-lm', '-o', str(binary)], check=True)
    original = b'#!/bin/sh\necho original\n'
    def reset():
        audio.write_bytes(original)
        audio.chmod(0o755)
        log.write_text('')
    def publish(state):
        data = {'working_status': state, 'working_status_source': 'robot_work_state_cache',
                'observer_monotonic_ms': time.monotonic()*1000,
                'cache_read_at': int(time.time()), 'local_link_received_at': int(time.time()),
                'error': 'NoError', 'working_state_code': 4, 'vendor_state_code': 4}
        tmp = d/'new.json'
        tmp.write_text(json.dumps(data))
        tmp.replace(status)
    def commands():
        return log.read_text().splitlines()
    def until(predicate, timeout=12):
        deadline = time.monotonic()+timeout
        while time.monotonic()<deadline:
            if predicate(): return
            time.sleep(.025)
        raise AssertionError('Timed out')
    def restored():
        return audio.exists() and audio.read_bytes()==original and not Path(str(audio)+'.saved').exists()

    reset()
    publish('Hibernating')
    p = subprocess.Popen([str(binary), 'wake'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    done = threading.Event()
    def observer():
        while not done.is_set():
            sent = commands()
            publish('Pause' if sent.count('38 0')>=2 else 'AutoClean' if '34 2' in sent else 'Hibernating')
            time.sleep(.1)
    t = threading.Thread(target=observer)
    t.start()
    out, err = p.communicate(timeout=12)
    done.set();t.join()
    assert p.returncode==0, (out,err)
    assert restored() and (audio.stat().st_mode & 0o777)==0o755
    assert commands().count('34 2')==1 and commands().count('38 0')==2

    # Caller SIGKILL must not kill the guardian or strand the mute.
    reset();publish('Hibernating')
    p = subprocess.Popen([str(binary), 'wake'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    until(lambda:'34 2' in commands())
    assert not restored()
    other = subprocess.run([str(binary),'wake'],capture_output=True,timeout=2)
    assert other.returncode==6, other.stderr
    p.kill()
    p.communicate(timeout=8)  # guardian closes inherited pipe after recovery
    until(restored)
    assert commands().count('34 2')==1 and commands().count('38 0')==2
    assert (audio.stat().st_mode & 0o777)==0o755

    # Preflight and an already-active robot never mount or send anything.
    reset();publish('AutoClean')
    for args in (['wake','--check'], ['wake']):
        r=subprocess.run([str(binary),*args],capture_output=True,timeout=3)
        assert r.returncode==0,(r.stdout,r.stderr)
        assert restored() and not commands()
    # Refuse a symlink target before sending clean.
    reset();publish('Hibernating');audio.unlink();audio.symlink_to('/dev/null')
    r=subprocess.run([str(binary),'wake'],capture_output=True,timeout=3)
    assert r.returncode==5 and not commands()
print('wake process: success, original mode, concurrent rejection, caller SIGKILL recovery, read-only check and target refusal PASS')
