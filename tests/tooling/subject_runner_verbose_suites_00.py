#!/usr/bin/env python3

"""Audit every normal alias for one named subject suite and an empty global suite."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MATRIX = json.loads(
    Path(__file__).with_name("subject_runner_matrix.json").read_text(encoding="utf-8")
)
TARGET_SUITES = {
    Path(path).stem: suite for path, suite in MATRIX["runners"].items()
}


def plain(text: str) -> str:
    return re.sub(r"\x1b\[[0-9;]*m", "", text)


def audit_global_suite(output: str, alias: str, rank: int) -> None:
    summaries = re.findall(
        r"Suite 'global': all tests passed \((\d+) asserts in (\d+) tests\)",
        output,
    )
    if summaries != [("0", "0")]:
        raise AssertionError(
            f"rank {rank} did not emit exactly global=0 asserts/0 tests for "
            f"{alias}:\n{output}"
        )


def audit_subject_suite(outputs: list[str], alias: str, suite: str) -> None:
    summaries = [
        summary
        for output in outputs
        for summary in re.findall(
            rf"Suite '{re.escape(suite)}': all tests passed \((\d+) asserts in (\d+) tests\)",
            output,
        )
    ]
    if not summaries or len(summaries) > len(outputs) or any(
        int(asserts) == 0 or int(tests) == 0 for asserts, tests in summaries
    ):
        raise AssertionError(
            f"expected active {suite!r} suite results for {alias}:\n"
            + "\n".join(outputs)
        )


def ctest_output(build_dir: Path, alias: str) -> str:
    completed = subprocess.run(
        [
            "ctest",
            "--test-dir",
            str(build_dir),
            "-R",
            f"^{re.escape(alias)}$",
            "--verbose",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    output = plain(completed.stdout + completed.stderr)
    if completed.returncode != 0:
        raise AssertionError(f"normal alias failed: {alias}\n{output}")
    return output


def rank_output_main(arguments: list[str]) -> None:
    separator = arguments.index("--")
    directory = Path(arguments[0]).resolve()
    command = arguments[separator + 1 :]
    rank = next(
        (
            os.environ[name]
            for name in ("PMI_RANK", "PMIX_RANK", "OMPI_COMM_WORLD_RANK")
            if name in os.environ
        ),
        None,
    )
    if rank is None or not rank.isdigit() or not command:
        raise SystemExit("rank-output wrapper requires an MPI rank and command")
    descriptor = os.open(
        directory / f"rank-{rank}.log",
        os.O_CREAT | os.O_WRONLY | os.O_TRUNC,
        0o600,
    )
    os.dup2(descriptor, sys.stdout.fileno())
    os.dup2(descriptor, sys.stderr.fileno())
    os.close(descriptor)
    os.execv(command[0], command)


def mpi_outputs(test: dict[str, object], entry: dict[str, object]) -> list[str]:
    properties = {
        prop["name"]: prop["value"] for prop in test.get("properties", [])
    }
    command = [str(argument) for argument in test["command"]]
    target_indices = [
        index
        for index, argument in enumerate(command)
        if Path(argument).name == entry["target"]
    ]
    if len(target_indices) != 1:
        raise AssertionError(f"cannot isolate MPI target in command: {command}")
    target_index = target_indices[0]
    with tempfile.TemporaryDirectory(prefix="rift-suite-audit-") as temporary:
        directory = Path(temporary)
        wrapped = command[:target_index] + [
            sys.executable,
            str(Path(__file__).resolve()),
            "--rank-output",
            str(directory),
            "--",
            *command[target_index:],
        ]
        completed = subprocess.run(
            wrapped,
            cwd=properties.get("WORKING_DIRECTORY"),
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode != 0:
            raise AssertionError(
                f"rank-separated alias failed: {entry['name']}\n"
                f"{completed.stdout}{completed.stderr}"
            )
        paths = [directory / f"rank-{rank}.log" for rank in range(entry["processors"])]
        if not all(path.is_file() for path in paths):
            raise AssertionError(
                f"rank-separated output is incomplete for {entry['name']}: "
                f"{sorted(map(str, directory.iterdir()))}"
            )
        return [plain(path.read_text(encoding="utf-8")) for path in paths]


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
    generated = {test["name"]: test for test in document["tests"]}
    normal = [
        entry
        for entry in MATRIX["ctests"]
        if not entry.get("fatal")
        and set(entry["labels"]).intersection({"serial", "mpi"})
    ]

    for entry in normal:
        outputs = (
            [ctest_output(build_dir, entry["name"])]
            if entry["processors"] == 1
            else mpi_outputs(generated[entry["name"]], entry)
        )
        for rank, output in enumerate(outputs):
            audit_global_suite(output, entry["name"], rank)
        suite = TARGET_SUITES[entry["target"]]
        audit_subject_suite(outputs, entry["name"], suite)

    for target in sorted({entry["target"] for entry in normal}):
        executable = build_dir / "tests" / target
        completed = subprocess.run(
            [str(executable), "unreviewed-mode"],
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode == 0:
            raise AssertionError(f"runner accepted an unknown selector: {target}")

    print(
        f"verbose suite audit: {len(normal)} normal aliases, "
        f"{len({entry['target'] for entry in normal})} fail-closed runners"
    )


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--rank-output":
        rank_output_main(sys.argv[2:])
    else:
        main()
