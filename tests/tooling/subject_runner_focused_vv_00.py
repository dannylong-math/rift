#!/usr/bin/env python3

"""Select the exact consolidated aliases that preserve the focused C1--C7 replay."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path


EXPECTED_ALIASES = {
    "mesh_snapshot_mpi_2_ranks",
    "mesh_snapshot_run_tests",
    "phase_graph_mpi_2_ranks",
    "phase_graph_mpi_3_ranks",
    "phase_graph_run_tests",
    "space_registry_mpi_2_ranks",
    "space_registry_mpi_3_ranks",
    "space_registry_run_tests",
    "state_snapshot_run_tests",
    "state_store_mpi_2_ranks",
    "state_store_mpi_3_ranks",
    "state_store_run_tests",
    "state_transaction_mpi_2_ranks",
    "state_transaction_mpi_3_ranks",
    "state_transaction_run_tests",
}


def focused_regex() -> str:
    return "^(" + "|".join(map(re.escape, sorted(EXPECTED_ALIASES))) + ")$"


def selected_tests(build_dir: Path, expression: str) -> set[str]:
    document = json.loads(
        subprocess.run(
            [
                "ctest",
                "--test-dir",
                str(build_dir),
                "--show-only=json-v1",
                "-R",
                expression,
            ],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    )
    return {test["name"] for test in document["tests"]}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--run", action="store_true")
    arguments = parser.parse_args()
    build_dir = arguments.build_dir.resolve()
    expression = focused_regex()
    actual = selected_tests(build_dir, expression)
    if not actual or actual != EXPECTED_ALIASES:
        raise AssertionError(
            "focused V&V selection mismatch: "
            f"missing={sorted(EXPECTED_ALIASES - actual)}, "
            f"extra={sorted(actual - EXPECTED_ALIASES)}"
        )

    if arguments.run:
        subprocess.run(
            [
                "ctest",
                "--test-dir",
                str(build_dir),
                "--output-on-failure",
                "-R",
                expression,
            ],
            check=True,
        )

    print(f"focused V&V selection: {len(actual)} exact nonempty aliases")


if __name__ == "__main__":
    main()
