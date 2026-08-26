#!/usr/bin/env python3

"""Resolve labeled CTests onto their instrumented test executables."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def labels(test: dict[str, object]) -> set[str]:
    for prop in test.get("properties", []):
        if prop["name"] == "LABELS":
            return set(prop["value"])
    return set()


def resolve_test_mapping(
    manifest: dict[str, object], discovered: set[Path]
) -> dict[Path, list[str]]:
    """Map every discovered executable to all labeled CTests that invoke it.

    A CTest invocation has exactly one instrumented test executable. Multiple
    invocations may intentionally share that executable, for example to test
    the same MPI protocol with different communicator sizes.
    """

    registered: dict[Path, list[str]] = {}
    for test in manifest["tests"]:
        test_labels = labels(test)
        if "tutorial" in test_labels:
            continue
        if not test_labels.intersection({"serial", "mpi", "coverage"}):
            continue
        name = test["name"]
        invoked = {
            Path(str(argument)).resolve()
            for argument in test.get("command", [])
            if Path(str(argument)).resolve() in discovered
        }
        if len(invoked) != 1:
            raise ValueError(
                f"CTest {name} does not invoke exactly one discovered "
                f"instrumented executable: found={sorted(map(str, invoked))}"
            )
        executable = invoked.pop()
        registered.setdefault(executable, []).append(name)

    unregistered = sorted(discovered - set(registered))
    if unregistered:
        raise ValueError(
            "instrumented executable/CTest mismatch: "
            f"unregistered executables={list(map(str, unregistered))}"
        )
    if not registered:
        raise ValueError("no serial, MPI, or coverage CTest executables were registered")

    return {
        executable: sorted(test_names)
        for executable, test_names in sorted(registered.items())
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument(
        "--emit-test-pairs",
        action="store_true",
        help="emit executable/name NUL pairs instead of unique executables",
    )
    arguments = parser.parse_args()
    build_dir = arguments.build_dir.resolve()

    completed = subprocess.run(
        ["ctest", "--test-dir", str(build_dir), "--show-only=json-v1"],
        check=True,
        capture_output=True,
        text=True,
    )
    manifest = json.loads(completed.stdout)
    discovered = {
        path.resolve()
        for path in (build_dir / "tests").iterdir()
        if path.is_file() and os.access(path, os.X_OK)
    }
    registered = resolve_test_mapping(manifest, discovered)

    for path, test_names in registered.items():
        if arguments.emit_test_pairs:
            for test_name in test_names:
                sys.stdout.buffer.write(os.fsencode(path) + b"\0")
                sys.stdout.buffer.write(os.fsencode(test_name) + b"\0")
        else:
            sys.stdout.buffer.write(os.fsencode(path) + b"\0")


if __name__ == "__main__":
    main()
