"""scripts/verify_exp.py 的退出码与 checksum 比较测试。"""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERIFY = ROOT / "scripts" / "verify_exp.py"
FIXTURES = ROOT / "tests" / "fixtures"
OK_BASELINE = FIXTURES / "fake_ok_baseline.sh"
OK_OPTIMIZED = FIXTURES / "fake_ok_optimized.sh"
BAD_OPTIMIZED = FIXTURES / "fake_bad_optimized.sh"


class VerifyTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)

    def run_verify(self, baseline, optimized):
        return subprocess.run(
            [sys.executable, str(VERIFY), str(baseline), str(optimized)],
            capture_output=True,
            text=True,
        )

    def write_script(self, name, body):
        path = Path(self._tmp.name) / name
        path.write_text("#!/usr/bin/env bash\n" + body, encoding="utf-8")
        path.chmod(0o755)
        return path

    def test_matching_checksums_exit_zero(self):
        proc = self.run_verify(OK_BASELINE, OK_OPTIMIZED)
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)
        self.assertIn("0x2a", proc.stdout)

    def test_mismatched_checksums_exit_one(self):
        proc = self.run_verify(OK_BASELINE, BAD_OPTIMIZED)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        combined = proc.stdout + proc.stderr
        self.assertIn("0x2a", combined)
        self.assertIn("0x2b", combined)

    def test_skip_exit_code_is_two(self):
        skip = self.write_script(
            "fake_skip.sh",
            "echo '{\"exp\":\"x\",\"variant\":\"baseline\","
            "\"skip\":\"缺少 liburing\"}'\nexit 2\n",
        )
        proc = self.run_verify(skip, OK_OPTIMIZED)
        self.assertEqual(proc.returncode, 2, msg=proc.stdout + proc.stderr)
        self.assertIn("SKIP", proc.stdout)

    def test_nonzero_exit_is_one(self):
        broken = self.write_script("fake_broken.sh", "exit 7\n")
        proc = self.run_verify(OK_BASELINE, broken)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)


if __name__ == "__main__":
    unittest.main()
