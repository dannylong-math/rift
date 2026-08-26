#!/usr/bin/env python3

"""Run combined subject registrations in reverse order in fresh processes."""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--runner",
        action="append",
        required=True,
        help="reviewed suite name and executable separated by an equals sign",
    )
    arguments = parser.parse_args()

    for specification in reversed(arguments.runner):
        suite, executable_text = specification.split("=", maxsplit=1)
        executable = Path(executable_text).resolve()
        completed = subprocess.run(
            [str(executable), "serial-reverse"],
            check=False,
            capture_output=True,
            text=True,
        )
        output = completed.stdout + completed.stderr
        plain_output = re.sub(r"\x1b\[[0-9;]*m", "", output)
        if completed.returncode != 0:
            raise AssertionError(
                f"reverse-order fresh process failed for {suite}:\n{output}"
            )
        if "Suite 'global': all tests passed (0 asserts in 0 tests)" not in plain_output:
            raise AssertionError(f"global suite was not empty for {suite}:\n{output}")
        if f"Suite '{suite}': all tests passed" not in plain_output:
            raise AssertionError(f"subject suite did not pass for {suite}:\n{output}")

    print(f"reverse registration order: {len(arguments.runner)} fresh subject processes")


if __name__ == "__main__":
    main()
