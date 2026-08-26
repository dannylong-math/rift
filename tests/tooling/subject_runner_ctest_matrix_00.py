#!/usr/bin/env python3

"""Compare generated CTest aliases and process contracts with the golden matrix."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MATRIX = json.loads(
    Path(__file__).with_name("subject_runner_matrix.json").read_text(encoding="utf-8")
)


def properties(test: dict[str, object]) -> dict[str, object]:
    return {entry["name"]: entry["value"] for entry in test.get("properties", [])}


def executable_name(command: list[str], build_dir: Path) -> str:
    discovered = {
        path.resolve(): path.name
        for path in (build_dir / "tests").iterdir()
        if path.is_file() and os.access(path, os.X_OK)
    }
    invoked = {
        Path(argument).resolve(): discovered[Path(argument).resolve()]
        for argument in command
        if Path(argument).resolve() in discovered
    }
    if len(invoked) != 1:
        raise AssertionError(f"CTest command does not resolve one test executable: {command}")
    return next(iter(invoked.values()))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    arguments = parser.parse_args()
    build_dir = arguments.build_dir.resolve()
    document = json.loads(
        subprocess.run(
            ["ctest", "--test-dir", str(build_dir), "--show-only=json-v1"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    )
    actual = {test["name"]: test for test in document["tests"]}
    expected_entries = [
        entry
        for entry in MATRIX["ctests"]
        if (build_dir / "tests" / entry["target"]).exists()
    ]
    expected_names = {entry["name"] for entry in expected_entries}
    covered_names = {
        name
        for name, test in actual.items()
        if set(properties(test).get("LABELS", [])).intersection({"serial", "mpi", "coverage"})
        and "tutorial" not in set(properties(test).get("LABELS", []))
    }
    if covered_names != expected_names:
        raise AssertionError(
            "labeled CTest matrix mismatch: "
            f"missing={sorted(expected_names - covered_names)}, "
            f"extra={sorted(covered_names - expected_names)}"
        )

    for expected in expected_entries:
        test = actual[expected["name"]]
        props = properties(test)
        command = [str(argument) for argument in test["command"]]
        if executable_name(command, build_dir) != expected["target"]:
            raise AssertionError(f"wrong target for {expected['name']}: {command}")
        if expected["mode"] not in command:
            raise AssertionError(f"missing exact mode for {expected['name']}: {command}")
        if set(props.get("LABELS", [])) != set(expected["labels"]):
            raise AssertionError(f"wrong labels for {expected['name']}: {props}")
        if int(props.get("PROCESSORS", 1)) != expected["processors"]:
            raise AssertionError(f"wrong process count for {expected['name']}: {props}")
        if expected["timeout"] and int(props.get("TIMEOUT", 0)) != expected["timeout"]:
            raise AssertionError(f"wrong timeout for {expected['name']}: {props}")
        fatal = expected.get("fatal")
        if fatal:
            marker = f"RIFT_FATAL_ORACLE operation={fatal['operation']} status={fatal['status']}"
            if marker not in command or "fatal_mpi_test_wrapper.py" not in " ".join(command):
                raise AssertionError(f"fatal wrapper contract mismatch for {expected['name']}: {command}")

    print(f"subject-runner CTest matrix: {len(expected_names)} reviewed aliases")


if __name__ == "__main__":
    main()
