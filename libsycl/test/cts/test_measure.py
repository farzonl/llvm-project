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


if __name__ == "__main__":
    unittest.main()
