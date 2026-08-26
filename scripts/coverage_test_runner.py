#!/usr/bin/env python3

"""Execute every CTest alias associated with one instrumented executable."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
from pathlib import Path


def run_ctest_aliases(
    build_dir: Path,
    executable: Path,
    test_names: list[str],
    raw_dir: Path,
) -> None:
    if not test_names:
        raise ValueError(f"no CTest invocation maps to {executable}")
    if len(test_names) != len(set(test_names)):
        raise ValueError(f"duplicate CTest aliases map to {executable}: {test_names}")

    for alias_index, test_name in enumerate(test_names):
        sanitized_test_name = re.sub(r"[^A-Za-z0-9_.-]+", "_", test_name)
        profile_prefix = (
            f"{executable.name}-{alias_index:03d}-{sanitized_test_name}-"
        )
        profiles_before = set(raw_dir.glob(f"{profile_prefix}*.profraw"))
        environment = os.environ.copy()
        environment["LLVM_PROFILE_FILE"] = str(
            raw_dir
            / (
                f"{executable.name}-{alias_index:03d}-{sanitized_test_name}"
                "-%m-%p.profraw"
            )
        )
        subprocess.run(
            [
                "ctest",
                "--test-dir",
                str(build_dir),
                "-R",
                f"^{re.escape(test_name)}$",
                "--output-on-failure",
            ],
            check=True,
            env=environment,
        )
        new_profiles = [
            profile
            for profile in raw_dir.glob(f"{profile_prefix}*.profraw")
            if profile not in profiles_before and profile.stat().st_size > 0
        ]
        if not new_profiles:
            raise ValueError(
                f"CTest alias {test_name!r} generated no new nonempty profile "
                f"for {executable}"
            )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--raw-dir", required=True, type=Path)
    parser.add_argument("--test-name", required=True, action="append")
    arguments = parser.parse_args()
    run_ctest_aliases(
        arguments.build_dir.resolve(),
        arguments.executable.resolve(),
        arguments.test_name,
        arguments.raw_dir.resolve(),
    )


if __name__ == "__main__":
    main()
