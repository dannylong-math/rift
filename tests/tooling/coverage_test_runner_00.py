#!/usr/bin/env python3

"""Verify that coverage executes every CTest alias of one executable."""

from __future__ import annotations

import importlib.util
import tempfile
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "coverage_test_runner", ROOT / "scripts" / "coverage_test_runner.py"
)
assert SPEC is not None and SPEC.loader is not None
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


with tempfile.TemporaryDirectory() as directory:
    root = Path(directory).resolve()
    build_dir = root / "build"
    raw_dir = root / "raw"
    executable = build_dir / "tests" / "shared_mpi_test"
    raw_dir.mkdir()

    aliases = ["shared mpi/two rank", "shared mpi:two rank"]

    def generate_profile(*_args: object, **kwargs: object) -> None:
        profile_pattern = Path(kwargs["env"]["LLVM_PROFILE_FILE"])
        profile = Path(str(profile_pattern).replace("%m", "signature").replace("%p", "42"))
        profile.write_bytes(b"profile")

    with mock.patch.object(RUNNER.subprocess, "run", side_effect=generate_profile) as run:
        RUNNER.run_ctest_aliases(
            build_dir,
            executable,
            aliases,
            raw_dir,
        )

    assert run.call_count == 2
    commands = [call.args[0] for call in run.call_args_list]
    assert commands == [
        [
            "ctest",
            "--test-dir",
            str(build_dir),
            "-R",
            f"^{RUNNER.re.escape(aliases[0])}$",
            "--output-on-failure",
        ],
        [
            "ctest",
            "--test-dir",
            str(build_dir),
            "-R",
            f"^{RUNNER.re.escape(aliases[1])}$",
            "--output-on-failure",
        ],
    ]
    expected_profiles = [
        raw_dir / "shared_mpi_test-000-shared_mpi_two_rank-%m-%p.profraw",
        raw_dir / "shared_mpi_test-001-shared_mpi_two_rank-%m-%p.profraw",
    ]
    for call, expected_profile in zip(run.call_args_list, expected_profiles):
        assert call.kwargs["check"] is True
        profile = call.kwargs["env"]["LLVM_PROFILE_FILE"]
        assert profile == str(expected_profile)
    assert len(set(expected_profiles)) == 2

    with mock.patch.object(RUNNER.subprocess, "run"):
        try:
            RUNNER.run_ctest_aliases(
                build_dir,
                executable,
                ["profileless_alias"],
                raw_dir,
            )
        except ValueError as error:
            assert "generated no new nonempty profile" in str(error)
        else:
            raise AssertionError("a profileless CTest alias must be rejected")

    try:
        RUNNER.run_ctest_aliases(build_dir, executable, [], raw_dir)
    except ValueError as error:
        assert "no CTest invocation" in str(error)
    else:
        raise AssertionError("an empty alias list must be rejected")

    try:
        RUNNER.run_ctest_aliases(
            build_dir,
            executable,
            ["duplicate_alias", "duplicate_alias"],
            raw_dir,
        )
    except ValueError as error:
        assert "duplicate CTest aliases" in str(error)
    else:
        raise AssertionError("duplicate aliases must be rejected")
