#!/usr/bin/env python3

"""Guard the reviewed public-only executable tutorial layout and CTest matrix."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MATRIX = json.loads(
    (ROOT / "tutorials" / "tutorial_matrix.json").read_text(encoding="utf-8")
)


def properties(test: dict[str, object]) -> dict[str, object]:
    return {entry["name"]: entry["value"] for entry in test.get("properties", [])}


def cmake_cache_value(build_dir: Path, name: str) -> str:
    cache = (build_dir / "CMakeCache.txt").read_text(encoding="utf-8")
    match = re.search(rf"^{re.escape(name)}:[^=]*=(.*)$", cache, re.MULTILINE)
    if match is None or not match.group(1):
        raise AssertionError(f"CMake cache does not define {name}")
    return match.group(1)


def verify_mpi_command(
    stem: str,
    command: list[str],
    mpi_launcher: str,
    mpi_rank_flag: str,
    processors: int,
) -> None:
    expected_prefix = [mpi_launcher, mpi_rank_flag, str(processors)]
    if command[:3] != expected_prefix:
        raise AssertionError(
            f"{stem} does not use the exact two-rank MPI launcher: {command}"
        )


def verify_mpi_command_mutants() -> None:
    invalid_commands = [
        ["/build/tutorials/tutorial-001"],
        ["/wrong/mpiexec", "-n", "2", "/build/tutorials/tutorial-001"],
        ["/reviewed/mpiexec", "--wrong", "2", "/build/tutorials/tutorial-001"],
        ["/reviewed/mpiexec", "-n", "1", "/build/tutorials/tutorial-001"],
    ]
    for command in invalid_commands:
        try:
            verify_mpi_command(
                "tutorial-001", command, "/reviewed/mpiexec", "-n", 2
            )
        except AssertionError as error:
            if "two-rank MPI launcher" not in str(error):
                raise
        else:
            raise AssertionError(f"invalid tutorial command was accepted: {command}")
    verify_mpi_command(
        "tutorial-001",
        ["/reviewed/mpiexec", "-n", "2", "/build/tutorials/tutorial-001"],
        "/reviewed/mpiexec",
        "-n",
        2,
    )


def verify_sources() -> None:
    entries = MATRIX["tutorials"]
    expected_sources = {ROOT / entry["source"] for entry in entries}
    actual_sources = set((ROOT / "tutorials").glob("tutorial-*.cpp"))
    if actual_sources != expected_sources:
        raise AssertionError(
            "tutorial source layout mismatch: "
            f"missing={sorted(map(str, expected_sources - actual_sources))}, "
            f"extra={sorted(map(str, actual_sources - expected_sources))}"
        )

    forbidden = {
        "implementation detail": r"\brift::detail\b",
        "test helper": r"(?:rift::test|rift_test|tests?/.*\.hpp)",
        "Boost.UT": r"boost/ut|boost::ut",
        "serial communicator": r"MPI_COMM_SELF",
        "assertion macro": r"\bassert\s*\(",
        "manual MPI lifecycle": r"\bMPI_(?:Init|Finalize)\s*\(",
    }
    marker = re.compile(r"^\s*// rift:snippet-(?:begin|end) (tutorial-\d{3}\.[a-z0-9][a-z0-9-]*)$", re.M)
    for entry in entries:
        source = ROOT / entry["source"]
        page = ROOT / entry["page"]
        if not source.is_file() or not page.is_file():
            raise AssertionError(f"missing tutorial source or page: {source}, {page}")
        text = source.read_text(encoding="utf-8")
        for label, pattern in forbidden.items():
            if re.search(pattern, text):
                raise AssertionError(f"{source} contains forbidden {label}")
        if "MPI_COMM_WORLD" not in text or "MPI_InitFinalize" not in text:
            raise AssertionError(f"{source} does not use the reviewed MPI-world RAII lifecycle")
        names = marker.findall(text)
        if not names or any(not name.startswith(entry["stem"] + ".") for name in names):
            raise AssertionError(f"{source} has missing or foreign tutorial snippet markers")
        if "collective_check" not in text or entry["completion"] not in text:
            raise AssertionError(f"{source} lacks its non-assert collective smoke oracle")


def verify_ctest(build_dir: Path) -> None:
    mpi_launcher = cmake_cache_value(build_dir, "MPIEXEC_EXECUTABLE")
    mpi_rank_flag = cmake_cache_value(build_dir, "MPIEXEC_NUMPROC_FLAG")
    document = json.loads(
        subprocess.run(
            ["ctest", "--test-dir", str(build_dir), "--show-only=json-v1"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    )
    tests = {test["name"]: test for test in document["tests"]}
    expected_names = {entry["stem"] for entry in MATRIX["tutorials"]}
    actual_names = {
        name
        for name, test in tests.items()
        if "tutorial" in set(properties(test).get("LABELS", []))
    }
    if actual_names != expected_names:
        raise AssertionError(
            f"tutorial CTest mismatch: missing={sorted(expected_names - actual_names)}, "
            f"extra={sorted(actual_names - expected_names)}"
        )

    executable_dir = (build_dir / "tutorials").resolve()
    discovered = {
        path.resolve()
        for path in executable_dir.iterdir()
        if path.is_file() and os.access(path, os.X_OK) and path.name.startswith("tutorial-")
    }
    expected_executables = {(executable_dir / name).resolve() for name in expected_names}
    if discovered != expected_executables:
        raise AssertionError(
            f"tutorial executable mismatch: expected={sorted(map(str, expected_executables))}, "
            f"actual={sorted(map(str, discovered))}"
        )

    mapped: set[Path] = set()
    for entry in MATRIX["tutorials"]:
        test = tests[entry["stem"]]
        props = properties(test)
        command = [str(argument) for argument in test["command"]]
        invoked = {path for path in discovered if str(path) in command}
        if len(invoked) != 1 or invoked & mapped:
            raise AssertionError(f"{entry['stem']} does not map exactly once: {command}")
        mapped.update(invoked)
        verify_mpi_command(
            entry["stem"],
            command,
            mpi_launcher,
            mpi_rank_flag,
            entry["processors"],
        )
        if set(props.get("LABELS", [])) != set(entry["labels"]):
            raise AssertionError(f"wrong labels for {entry['stem']}: {props}")
        if int(props.get("PROCESSORS", 0)) != entry["processors"]:
            raise AssertionError(f"wrong process count for {entry['stem']}: {props}")
        if int(props.get("TIMEOUT", 0)) != entry["timeout"]:
            raise AssertionError(f"wrong timeout for {entry['stem']}: {props}")
        pass_regex = props.get("PASS_REGULAR_EXPRESSION", [])
        if entry["completion"] not in pass_regex:
            raise AssertionError(f"missing completion oracle for {entry['stem']}: {props}")
    if mapped != discovered:
        raise AssertionError(f"orphan tutorial executables: {sorted(map(str, discovered - mapped))}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path)
    arguments = parser.parse_args()
    verify_mpi_command_mutants()
    verify_sources()
    if arguments.build_dir is not None:
        verify_ctest(arguments.build_dir.resolve())
    print("tutorial layout: 3 public-only MPI tutorials match the reviewed matrix")


if __name__ == "__main__":
    main()
