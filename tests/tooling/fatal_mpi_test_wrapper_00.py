#!/usr/bin/env python3

"""Exercise the fatal MPI child-result oracle without launching MPI."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import fatal_mpi_test_wrapper as wrapper


def rejected(return_code: int, output: str) -> bool:
    try:
        wrapper.validate_child_result(
            return_code=return_code,
            output=output,
            expected_marker="RIFT_FATAL_ORACLE operation=begin status=34",
        )
    except ValueError:
        return True
    return False


assert rejected(0, "RIFT_FATAL_ORACLE operation=begin status=34\n")
assert rejected(34, "")
assert rejected(34, "RIFT_FATAL_SETUP operation=begin status=34\n")
assert rejected(34, "RIFT_FATAL_ORACLE operation=begin status=15\n")
assert rejected(34, "RIFT_FATAL_ORACLE operation=finalize status=34\n")
assert rejected(1, "RIFT_FATAL_ORACLE operation=begin status=34\n")
assert rejected(
    1,
    "RIFT_FATAL_ORACLE operation=begin status=34\n"
    "application called MPI_Abort(MPI_COMM_WORLD, 15) - process 1\n",
)

wrapper.validate_child_result(
    return_code=34,
    output=(
        "MPI preamble\nRIFT_FATAL_ORACLE operation=begin status=34\n"
        "Abort(34) on node 1: application called MPI_Abort(MPI_COMM_WORLD, 34) - process 1\n"
    ),
    expected_marker="RIFT_FATAL_ORACLE operation=begin status=34",
)

wrapper.validate_child_result(
    return_code=1,
    output=(
        "RIFT_FATAL_ORACLE operation=begin status=34\n"
        "MPI_ABORT was invoked on rank 1 in communicator MPI_COMM_WORLD\n"
        "with errorcode 34.\n"
    ),
    expected_marker="RIFT_FATAL_ORACLE operation=begin status=34",
)
