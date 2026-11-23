#!/usr/bin/env python3
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Measure unchanged upstream CTS targets without hiding failed builds or runs."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess
import sys


def parse_test_totals(text):
    passed = re.search(
        r"^All tests passed \((\d+) assertions? in (\d+) test cases?\)[ \t]*\r?$",
        text, re.MULTILINE,
    )
    if passed:
        assertions, cases = map(int, passed.groups())
        return {
            name: {"total": count, "passed": count, "failed": 0,
                   "failed_as_expected": 0}
            for name, count in (("assertions", assertions), ("test_cases", cases))
        }
    totals = {}
    for label, body in re.findall(
        r"^(test cases|assertions):\s*([^\n]+)", text, re.MULTILINE
    ):
        columns = [column.strip() for column in body.split("|")]
        counts = {"passed": 0, "failed": 0, "failed_as_expected": 0}
        if columns[0] == "- none -":
            counts["total"] = 0
        elif columns[0].isdigit():
            counts["total"] = int(columns[0])
        else:
            return None
        for column in columns[1:]:
            match = re.fullmatch(r"(\d+) (passed|failed|failed as expected)", column)
            if not match:
                return None
            counts[match[2].replace(" ", "_")] = int(match[1])
        if counts["total"] != sum(counts[key] for key in
                                  ("passed", "failed", "failed_as_expected")):
            return None
        totals[label.replace(" ", "_")] = counts
    return totals if set(totals) == {"test_cases", "assertions"} else None


def summarize(results, configured, devices):
    summary = {
        "compiled_categories": sum(
            entry["compile"]["status"] == "passed" for entry in results
        ),
        "executable_categories": sum(
            entry.get("executable_build", {}).get("status") == "passed"
            for entry in results
        ),
        "categories_passing_all_devices": sum(
            len(entry["runs"]) == len(devices) and
            all(run["status"] == "passed" for run in entry["runs"])
            for entry in results
        ),
        "unmeasured_categories": configured - len(results),
        "per_device": {},
    }
    for device in devices:
        runs = [run for entry in results for run in entry["runs"]
                if run["device"] == device]
        known = [run["test_totals"] for run in runs if run.get("test_totals")]
        summary["per_device"][device] = {
            "categories_passed": sum(run["status"] == "passed" for run in runs),
            "categories_failed": sum(run["status"] == "failed" for run in runs),
            "infrastructure_failures": sum(
                run["status"] == "infrastructure_failure" for run in runs
            ),
            "categories_not_run": len(results) - len(runs),
            "runs_with_totals": len(known),
            "runs_without_totals": len(runs) - len(known),
            "counts_complete": bool(runs) and len(known) == len(runs),
            **{
                name: {
                    key: sum(totals[name][key] for totals in known)
                    for key in ("total", "passed", "failed", "failed_as_expected")
                }
                for name in ("test_cases", "assertions")
            },
        }
    return summary


def run_command(command, log, timeout=None):
    try:
        with log.open("w", encoding="utf-8") as output:
            result = subprocess.run(
                command,
                stdout=output,
                stderr=subprocess.STDOUT,
                timeout=timeout,
                check=False,
            )
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"status": "infrastructure_failure", "error": str(error)}
    text = log.read_text(encoding="utf-8", errors="replace")
    diagnostics = [
        line
        for line in text.splitlines()
        if re.search(r"\berror:|\berror C\d+:", line)
    ]
    return {
        "status": "passed" if result.returncode == 0 else "failed",
        "returncode": result.returncode,
        "diagnostics": diagnostics[:20],
    }


def measure(manifest, build_dir, report_dir, targets, devices, jobs, timeout):
    report_dir.mkdir(parents=True, exist_ok=True)
    results = []
    failed = False
    for test in manifest["tests"]:
        name = test["target"]
        if name not in targets:
            continue
        entry = {"target": name, "runs": []}
        for stage, target in (
            ("compile", test["object_target"]),
            ("executable_build", name),
        ):
            log = report_dir / f"{name}-{stage}.log"
            result = run_command(
                [
                    "cmake",
                    "--build",
                    str(build_dir),
                    "--target",
                    target,
                    "--parallel",
                    str(jobs),
                ],
                log,
            )
            entry[stage] = {"log": str(log), **result}
            if result["status"] != "passed":
                failed = True
                break
        else:
            for index, device in enumerate(devices):
                log = report_dir / f"{name}-device-{index}.log"
                info = report_dir / f"{name}-device-{index}.info"
                result = run_command(
                    [
                        test["executable"],
                        "--device",
                        device,
                        "--info-dump",
                        str(info),
                    ],
                    log,
                    timeout,
                )
                result["test_totals"] = (
                    parse_test_totals(log.read_text(encoding="utf-8", errors="replace"))
                    if log.exists() else None
                )
                entry["runs"].append(
                    {"device": device, "log": str(log), **result}
                )
                failed |= result["status"] != "passed"
        results.append(entry)
        print(
            f"{name}: compile={entry['compile']['status']}, "
            f"executable_build={entry.get('executable_build', {}).get('status', 'not_run')}, "
            f"devices_run={len(entry['runs'])}",
            flush=True,
        )
    return results, failed


def positive_integer(value):
    value = int(value)
    if value < 1:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return value


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--report-dir", type=Path, required=True)
    parser.add_argument("--targets", nargs="+", help="Default: all configured targets")
    parser.add_argument("--devices", nargs="+", required=True, help="CTS device name filters")
    parser.add_argument("--jobs", type=positive_integer, default=1)
    parser.add_argument(
        "--timeout", type=positive_integer, default=300,
        help="Runtime timeout per test executable, in seconds",
    )
    args = parser.parse_args(argv)
    build_dir = args.build_dir.resolve()
    report_dir = args.report_dir.resolve()
    try:
        manifest = json.loads(
            (build_dir / "libsycl-cts-targets.json").read_text(encoding="utf-8")
        )
        available = {test["target"] for test in manifest["tests"]}
        targets = set(args.targets) if args.targets else available
        if not targets or targets - available:
            parser.error(f"unknown or empty CTS target selection: {sorted(targets - available)}")
        revision = subprocess.run(
            ["git", "-C", manifest["source_dir"], "rev-parse", "HEAD"],
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
        results, failed = measure(
            manifest, build_dir, report_dir, targets, args.devices, args.jobs, args.timeout
        )
        report = {
            "schema_version": 1,
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "cts_revision": revision,
            "compiler": manifest["compiler"],
            "full_conformance": manifest["full_conformance"],
            "deprecated_features": manifest["deprecated_features"],
            "opencl_interop": manifest["opencl_interop"],
            "configured_targets": len(available),
            "selected_targets": len(targets),
            "devices": args.devices,
            "results": results,
            "summary": summarize(results, len(available), args.devices),
        }
        destination = report_dir / "baseline.json"
        destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(2, f"CTS baseline infrastructure failure: {error}\n")
    print(f"Report: {destination}")
    if any(
        entry.get(stage, {}).get("status") == "infrastructure_failure"
        for entry in results
        for stage in ("compile", "executable_build")
    ) or any(
        run["status"] == "infrastructure_failure"
        for entry in results
        for run in entry["runs"]
    ):
        return 2
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
