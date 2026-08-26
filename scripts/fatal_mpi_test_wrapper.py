#!/usr/bin/env python3

"""Run an MPI fatal-path child and validate its explicit abort marker."""

from __future__ import annotations

import argparse
import os
import re
import signal
import subprocess
import sys
from collections.abc import Sequence


def validate_child_result(*, return_code: int, output: str, expected_marker: str) -> None:
    """Require both Rift's marker and the launcher's native MPI-abort evidence."""

    if return_code == 0:
        raise ValueError("fatal-path MPI child exited successfully")
    if expected_marker not in output.splitlines():
        raise ValueError(
            "fatal-path MPI child did not emit the exact expected marker: "
            f"{expected_marker!r}"
        )
    status_match = re.search(r"\bstatus=(\d+)\b", expected_marker)
    if status_match is None:
        raise ValueError(f"expected marker has no numeric status: {expected_marker!r}")
    status = re.escape(status_match.group(1))
    mpich_abort = re.search(
        rf"application called MPI_Abort\([^\n]*,\s*{status}\s*\)",
        output,
        re.IGNORECASE,
    )
    openmpi_abort = re.search(
        rf"MPI_ABORT was invoked.*?errorcode\s+{status}\b",
        output,
        re.IGNORECASE | re.DOTALL,
    )
    if mpich_abort is None and openmpi_abort is None:
        raise ValueError(
            "fatal-path MPI child emitted the Rift marker without matching "
            f"native MPI_Abort evidence for status {status_match.group(1)}"
        )


def run_child(command: Sequence[str], timeout_seconds: float) -> tuple[int, str]:
    """Run one launcher command and terminate its process group on timeout."""

    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        start_new_session=os.name == "posix",
    )
    try:
        output, _ = process.communicate(timeout=timeout_seconds)
    except subprocess.TimeoutExpired as error:
        if os.name == "posix":
            os.killpg(process.pid, signal.SIGKILL)
        else:
            process.kill()
        output, _ = process.communicate()
        raise TimeoutError(
            f"fatal-path MPI child exceeded {timeout_seconds:g} seconds\n{output}"
        ) from error
    return process.returncode, output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--expected-marker", required=True)
    parser.add_argument("--timeout-seconds", type=float, default=10.0)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    arguments = parser.parse_args()
    command = arguments.command
    if command[:1] == ["--"]:
        command = command[1:]
    if not command:
        parser.error("an MPI launcher command is required after --")

    try:
        return_code, output = run_child(command, arguments.timeout_seconds)
        validate_child_result(
            return_code=return_code,
            output=output,
            expected_marker=arguments.expected_marker,
        )
    except (OSError, TimeoutError, ValueError) as error:
        print(error, file=sys.stderr)
        if "output" in locals():
            sys.stderr.write(output)
        return 1

    sys.stdout.write(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
