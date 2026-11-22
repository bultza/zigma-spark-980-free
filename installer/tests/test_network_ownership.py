import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

class NetworkOwnershipTest(unittest.TestCase):
    def test_native_owner_detection(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / "test-network"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "src/robot/tests/test_network_ownership.c"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=15)

if __name__ == "__main__":
    unittest.main()
