#!/usr/bin/env python3

"""Unit tests for the instrumented executable/CTest manifest contract."""

from __future__ import annotations

import importlib.util
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "coverage_test_manifest", ROOT / "scripts" / "coverage_test_manifest.py"
)
assert SPEC is not None and SPEC.loader is not None
MANIFEST = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MANIFEST)


def ctest(
    name: str, executable: Path | list[Path], labels: list[str] | None = None
) -> dict[str, object]:
    command = executable if isinstance(executable, list) else [executable]
    return {
        "name": name,
        "command": [str(argument) for argument in command],
        "properties": [{"name": "LABELS", "value": labels or ["mpi"]}],
    }


def require_value_error(fragment: str, operation: object) -> None:
    try:
        operation()
    except ValueError as error:
        assert fragment in str(error), str(error)
    else:
        raise AssertionError(f"expected ValueError containing {fragment!r}")


with tempfile.TemporaryDirectory() as directory:
    build_dir = Path(directory).resolve()
    tests_dir = build_dir / "tests"
    tests_dir.mkdir()
    shared = (tests_dir / "shared_mpi_test").resolve()
    orphan = (tests_dir / "orphan_test").resolve()
    unknown = (tests_dir / "unknown_test").resolve()
    coverage = (tests_dir / "coverage_guard").resolve()

    shared.write_text("", encoding="utf-8")
    orphan.write_text("", encoding="utf-8")
    coverage.write_text("", encoding="utf-8")
    shared.chmod(0o755)
    orphan.chmod(0o755)
    coverage.chmod(0o755)

    aliases = {
        "tests": [
            ctest("shared_mpi_two_rank", shared),
            ctest("shared_mpi_three_rank", shared),
        ]
    }
    mapping = MANIFEST.resolve_test_mapping(aliases, {shared})
    assert mapping == {shared: ["shared_mpi_three_rank", "shared_mpi_two_rank"]}

    all_instrumented_labels = {
        "tests": [
            ctest("serial_test", shared, ["serial"]),
            ctest("coverage_test", coverage, ["coverage"]),
        ]
    }
    mapping = MANIFEST.resolve_test_mapping(
        all_instrumented_labels, {shared, coverage}
    )
    assert mapping == {coverage: ["coverage_test"], shared: ["serial_test"]}

    tutorial_labels_are_outside_unit_coverage = {
        "tests": [
            ctest("serial_test", shared, ["serial"]),
            ctest("tutorial-001", unknown, ["tutorial", "mpi"]),
        ]
    }
    mapping = MANIFEST.resolve_test_mapping(
        tutorial_labels_are_outside_unit_coverage, {shared}
    )
    assert mapping == {shared: ["serial_test"]}

    wrong_executable = {"tests": [ctest("wrong", unknown)]}
    require_value_error(
        "does not invoke exactly one discovered instrumented executable",
        lambda: MANIFEST.resolve_test_mapping(wrong_executable, {shared}),
    )

    unregistered = {"tests": [ctest("registered", shared)]}
    require_value_error(
        "unregistered executables",
        lambda: MANIFEST.resolve_test_mapping(unregistered, {shared, orphan}),
    )

    two_executables = {
        "tests": [ctest("ambiguous", [shared, orphan])]
    }
    require_value_error(
        "does not invoke exactly one discovered instrumented executable",
        lambda: MANIFEST.resolve_test_mapping(
            two_executables, {shared, orphan}
        ),
    )

    require_value_error(
        "no serial, MPI, or coverage CTest executables were registered",
        lambda: MANIFEST.resolve_test_mapping({"tests": []}, set()),
    )
