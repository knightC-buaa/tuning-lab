"""scripts/report.py 的汇总规则测试。"""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / "scripts" / "report.py"


def row(exp, variant, median_ns, checksum="0x2a", **extra):
    data = {
        "exp": exp,
        "variant": variant,
        "median_ns": median_ns,
        "checksum": checksum,
    }
    data.update(extra)
    return data


class ReportTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)

    def write_jsonl(self, name, rows):
        path = Path(self._tmp.name) / name
        path.write_text(
            "".join(json.dumps(r, ensure_ascii=False) + "\n" for r in rows),
            encoding="utf-8",
        )
        return path

    def run_report(self, *paths):
        proc = subprocess.run(
            [sys.executable, str(REPORT), *map(str, paths)],
            capture_output=True,
            text=True,
        )
        self.assertEqual(proc.returncode, 0, msg=proc.stderr)
        return proc.stdout

    def test_significant(self):
        out = self.run_report(
            self.write_jsonl(
                "raw.jsonl",
                [
                    row("2.1.1_hash_vs_scan", "baseline", 200),
                    row("2.1.1_hash_vs_scan", "optimized", 100),
                ],
            )
        )
        self.assertIn("2.00x", out)
        self.assertIn("| SIGNIFICANT |", out)
        self.assertIn("## 2 计算", out)

    def test_inconclusive_when_intervals_overlap(self):
        out = self.run_report(
            self.write_jsonl(
                "raw.jsonl",
                [
                    row("2.2.1_overlap", "baseline", 60),
                    row("2.2.1_overlap", "baseline", 100),
                    row("2.2.1_overlap", "baseline", 140),
                    row("2.2.1_overlap", "optimized", 65),
                    row("2.2.1_overlap", "optimized", 105),
                    row("2.2.1_overlap", "optimized", 145),
                ],
            )
        )
        self.assertIn("| INCONCLUSIVE |", out)

    def test_regression(self):
        out = self.run_report(
            self.write_jsonl(
                "raw.jsonl",
                [
                    row("2.3.1_slower", "baseline", 200),
                    row("2.3.1_slower", "optimized", 250),
                ],
            )
        )
        self.assertIn("0.80x", out)
        self.assertIn("| REGRESSION |", out)

    def test_checksum_mismatch_is_invalid(self):
        out = self.run_report(
            self.write_jsonl(
                "raw.jsonl",
                [
                    row("2.4.1_mismatch", "baseline", 200, checksum="0x2a"),
                    row("2.4.1_mismatch", "optimized", 100, checksum="0x2b"),
                ],
            )
        )
        self.assertIn("| INVALID |", out)

    def test_skip_row(self):
        out = self.run_report(
            self.write_jsonl(
                "raw.jsonl",
                [
                    {
                        "exp": "4.3.2_io_uring",
                        "variant": "optimized",
                        "skip": "缺少 liburing",
                    }
                ],
            )
        )
        self.assertIn("| SKIP |", out)
        self.assertIn("缺少 liburing", out)

    def test_lower_bound_marker_for_sub_100us(self):
        out = self.run_report(
            self.write_jsonl(
                "raw.jsonl",
                [
                    row("2.5.1_floor", "baseline", 1_000_000_000),
                    row("2.5.1_floor", "optimized", 50_000),
                ],
            )
        )
        self.assertIn("≥", out)
        self.assertIn("| SIGNIFICANT |", out)

    def test_verify_rows_are_ignored(self):
        out = self.run_report(
            self.write_jsonl(
                "raw.jsonl",
                [
                    row(
                        "2.6.1_verify",
                        "baseline",
                        0,
                        checksum="0xdead",
                        verify=True,
                    ),
                    row("2.6.1_verify", "baseline", 200, checksum="0x2a"),
                    row("2.6.1_verify", "optimized", 100, checksum="0x2a"),
                ],
            )
        )
        self.assertIn("| SIGNIFICANT |", out)
        self.assertNotIn("| INVALID |", out)

    def test_missing_variant_is_invalid_with_note(self):
        out = self.run_report(
            self.write_jsonl(
                "raw.jsonl",
                [row("2.7.1_missing", "optimized", 100)],
            )
        )
        self.assertIn("| INVALID |", out)
        self.assertIn("缺少 baseline", out)

    def test_grouping_by_first_digit(self):
        rows = []
        for exp in ("2.1_a", "3.1_b", "4.1_c", "5.1_d", "6.1_e", "7.1_f"):
            rows.append(row(exp, "baseline", 200))
            rows.append(row(exp, "optimized", 100))
        out = self.run_report(self.write_jsonl("raw.jsonl", rows))
        for heading in (
            "## 2 计算",
            "## 3 存储",
            "## 4 I/O",
            "## 5 并发",
            "## 6 分布式",
            "## 其他",
        ):
            self.assertIn(heading, out)

    def test_geometric_mean_of_valid_speedups(self):
        out = self.run_report(
            self.write_jsonl(
                "raw.jsonl",
                [
                    row("2.8.1_two", "baseline", 200),
                    row("2.8.1_two", "optimized", 100),
                    row("2.8.2_eight", "baseline", 800),
                    row("2.8.2_eight", "optimized", 100),
                ],
            )
        )
        self.assertIn("几何平均加速比：4.00x", out)

    def test_multiple_jsonl_files_are_combined(self):
        first = self.write_jsonl(
            "baseline.jsonl", [row("2.9.1_split", "baseline", 200)]
        )
        second = self.write_jsonl(
            "optimized.jsonl", [row("2.9.1_split", "optimized", 100)]
        )
        out = self.run_report(first, second)
        self.assertIn("| SIGNIFICANT |", out)

    def test_empty_input_produces_no_rows(self):
        empty = self.write_jsonl("raw.jsonl", [])
        out = self.run_report(empty)
        self.assertIn("没有实验数据", out)
        self.assertNotIn("| 实验 |", out)


if __name__ == "__main__":
    unittest.main()
