import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).parents[1] / "robot/src/zigma-mode-backend"

class PairingDHCPTests(unittest.TestCase):
    def run_case(self, ids="123", args=b"udhcpd\0-f\0/tmp/udhcpd.conf\0", config="interface wlan0\n", symlink=False, wrong_exe=False, stop=False, replaced=False):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            proc = root / "proc/123"
            proc.mkdir(parents=True)
            executable = root / "udhcpd"
            executable.touch()
            (proc / "exe").symlink_to(root / "foreign" if wrong_exe else executable)
            (proc / "cmdline").write_bytes(args)
            conf = root / "config"
            conf.write_text(config)
            if symlink:
                conf.rename(root / "target")
                conf.symlink_to(root / "target")
            text = SOURCE.read_text()
            verify = text[text.index("verify() {"):text.index("stop_exact() {")]
            funcs = text[text.index("pairing_dhcp_validate() {"):text.index("start_trial_observer() {")]
            # Only filesystem operands are redirected; argv must still match native paths.
            script = verify + funcs
            script = script.replace('"/proc/', '"' + str(root / "proc") + '/')
            script = script.replace(' /usr/sbin/udhcpd', ' ' + str(executable))
            script = script.replace(' /tmp/udhcpd.conf', ' ' + str(conf))
            script = "set -eu\n" + script + "\npidof() { printf '%s' \"$TEST_IDS\"; }\n"
            script += "start() { echo 777; }\nstop_exact() { echo STOP; TEST_IDS=" + ("456" if replaced else "") + "; }\n"
            script += "pairing_dhcp_stop\n" if stop else "pairing_dhcp_validate\n"
            env = dict(os.environ, TEST_IDS=ids)
            return subprocess.run(["sh", "-c", script], env=env, capture_output=True, text=True)

    def test_native_server(self):
        self.assertEqual(self.run_case().returncode, 0)
    def test_absent(self):
        r = self.run_case(ids="", stop=True)
        self.assertEqual(r.returncode, 0)
        self.assertNotIn("STOP", r.stdout)
    def test_ambiguous_or_foreign(self):
        for kw in [dict(ids="123 124"), dict(wrong_exe=True), dict(symlink=True), dict(config="interface wlan1\n"), dict(config="interface wlan0\ninterface wlan0\n"), dict(config=""), dict(args=b"udhcpd\0-f\0/other.conf\0")]:
            with self.subTest(kw=kw):
                r = self.run_case(stop=True, **kw)
                self.assertNotEqual(r.returncode, 0)
                self.assertNotIn("STOP", r.stdout)
    def test_stop_and_verify_absence(self):
        r = self.run_case(stop=True)
        self.assertEqual(r.returncode, 0)
        self.assertIn("STOP", r.stdout)
    def test_replacement_is_not_success(self):
        self.assertNotEqual(self.run_case(stop=True, replaced=True).returncode, 0)

if __name__ == "__main__":
    unittest.main()
