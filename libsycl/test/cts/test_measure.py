#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from pathlib import Path
import json
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import measure


class MeasurementTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.manifest = {
            "source_dir": str(self.root),
            "compiler": "clang-cl",
            "full_conformance": True,
            "deprecated_features": True,
            "opencl_interop": False,
            "tests": [
                {
                    "target": "test_example",
                    "object_target": "test_example_objects",
                    "executable": str(self.root / "stale.exe"),
                }
            ]
        }

    def test_failed_compile_does_not_run_stale_executable(self):
        (self.root / "stale.exe").write_bytes(b"old executable")
        with patch.object(
            measure, "run_command",
            return_value={"status": "failed", "returncode": 1},
        ) as run:
            results, failed = measure.measure(
                self.manifest, self.root, self.root, {"test_example"},
                ["NVIDIA"], 1, 300,
            )
        self.assertTrue(failed)
        self.assertEqual(run.call_count, 1)
        self.assertNotIn("executable_build", results[0])
        self.assertEqual(results[0]["runs"], [])

    def test_run_failure_does_not_hide_other_device_results(self):
        with patch.object(
            measure, "run_command",
            side_effect=[
                {"status": "passed", "returncode": 0},
                {"status": "passed", "returncode": 0},
                {"status": "failed", "returncode": 1},
                {"status": "passed", "returncode": 0},
            ],
        ) as run:
            results, failed = measure.measure(
                self.manifest, self.root, self.root, {"test_example"},
                ["NVIDIA", "AMD"], 1, 300,
            )
        self.assertTrue(failed)
        self.assertEqual(
            [result["status"] for result in results[0]["runs"]],
            ["failed", "passed"],
        )
        self.assertEqual(run.call_count, 4)
        self.assertEqual(run.call_args_list[2].args[0][1:3], ["--device", "NVIDIA"])
        self.assertEqual(run.call_args_list[3].args[0][1:3], ["--device", "AMD"])

    def test_missing_tool_is_an_infrastructure_failure(self):
        with patch.object(measure.subprocess, "run", side_effect=OSError("missing tool")):
            result = measure.run_command(["missing-tool"], self.root / "tool.log")
        self.assertEqual(result["status"], "infrastructure_failure")
        self.assertIn("missing tool", result["error"])

    def test_command_infrastructure_failure_has_distinct_exit_status(self):
        (self.root / "libsycl-cts-targets.json").write_text(
            json.dumps(self.manifest), encoding="utf-8",
        )
        with patch.object(
            measure.subprocess, "run",
            return_value=subprocess.CompletedProcess([], 0, stdout="revision\n"),
        ), patch.object(
            measure, "run_command",
            return_value={"status": "infrastructure_failure", "error": "missing cmake"},
        ):
            status = measure.main([
                "--build-dir", str(self.root),
                "--report-dir", str(self.root / "report"),
                "--devices", "NVIDIA",
            ])
        self.assertEqual(status, 2)
        report = json.loads((self.root / "report" / "baseline.json").read_text())
        self.assertEqual(
            report["results"][0]["compile"]["status"], "infrastructure_failure",
        )

    def test_parses_passed_failed_and_zero_assertion_totals(self):
        passed = measure.parse_test_totals(
            "All tests passed (558 assertions in 6 test cases)\n"
        )
        self.assertEqual(passed["test_cases"]["passed"], 6)
        self.assertEqual(passed["assertions"]["passed"], 558)
        failed = measure.parse_test_totals(
            "test cases:    5 |    4 passed | 1 failed\n"
            "assertions: 8947 | 8946 passed | 1 failed\n"
        )
        self.assertEqual(failed["assertions"]["passed"], 8946)
        self.assertEqual(failed["test_cases"]["failed"], 1)
        empty = measure.parse_test_totals(
            "test cases: 1 | 1 passed\nassertions: - none -\n"
        )
        self.assertEqual(empty["assertions"]["total"], 0)
        self.assertIsNone(measure.parse_test_totals("crashed before summary\n"))
        self.assertIsNone(measure.parse_test_totals(
            "test cases: 5 | 4 passed\nassertions: 3 | 3 passed\n"
        ))
        expected = measure.parse_test_totals(
            "test cases: 2 | 1 passed | 1 failed as expected\r\n"
            "assertions: 3 | 2 passed | 1 failed as expected\r\n"
        )
        self.assertEqual(expected["assertions"]["failed_as_expected"], 1)

    def test_summary_distinguishes_partial_execution_and_unmeasured_categories(self):
        totals = measure.parse_test_totals(
            "All tests passed (2 assertions in 1 test case)"
        )
        results = [
            {"compile": {"status": "passed"},
             "executable_build": {"status": "passed"}, "runs": [
                 {"device": "NVIDIA", "status": "passed", "test_totals": totals},
                 {"device": "AMD", "status": "failed", "test_totals": None},
             ]},
            {"compile": {"status": "failed"}, "runs": []},
        ]
        summary = measure.summarize(results, 97, ["NVIDIA", "AMD"])
        self.assertEqual(summary["categories_passing_all_devices"], 0)
        self.assertEqual(summary["unmeasured_categories"], 95)
        self.assertEqual(summary["per_device"]["NVIDIA"]["assertions"]["passed"], 2)
        self.assertEqual(summary["per_device"]["NVIDIA"]["categories_not_run"], 1)
        self.assertFalse(summary["per_device"]["AMD"]["counts_complete"])
        self.assertEqual(summary["per_device"]["AMD"]["runs_without_totals"], 1)
        not_run = measure.summarize(results[1:], 97, ["NVIDIA"])
        self.assertFalse(not_run["per_device"]["NVIDIA"]["counts_complete"])

    def test_runtime_logs_attach_totals_without_inventing_missing_counts(self):
        def run(command, log, timeout=None):
            if timeout:
                if command[2] == "NVIDIA":
                    log.write_text("All tests passed (2 assertions in 1 test case)\n")
                else:
                    log.write_text("aborted before summary\n")
            return {"status": "passed", "returncode": 0}

        with patch.object(measure, "run_command", side_effect=run):
            results, _ = measure.measure(
                self.manifest, self.root, self.root, {"test_example"},
                ["NVIDIA", "AMD"], 1, 300,
            )
        self.assertEqual(results[0]["runs"][0]["test_totals"]["assertions"]["total"], 2)
        self.assertIsNone(results[0]["runs"][1]["test_totals"])

if __name__ == "__main__":
    unittest.main()
