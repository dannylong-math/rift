#!/usr/bin/env python3

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import merge_llvm_cov_exports as coverage


def expect_incomplete(totals, metric):
    try:
        coverage.require_authoritative_complete(totals)
    except ValueError as error:
        assert metric in str(error)
    else:
        raise AssertionError(f"uncovered authoritative {metric} passed the gate")


expect_incomplete(
    coverage.CoverageTotals(
        hit_lines=1,
        lines=2,
        hit_functions=1,
        functions=1,
        hit_branches=2,
        branches=2,
    ),
    "lines",
)
expect_incomplete(
    coverage.CoverageTotals(
        hit_lines=2,
        lines=2,
        hit_functions=0,
        functions=1,
        hit_branches=2,
        branches=2,
    ),
    "functions",
)
expect_incomplete(
    coverage.CoverageTotals(
        hit_lines=2,
        lines=2,
        hit_functions=1,
        functions=1,
        hit_branches=1,
        branches=2,
    ),
    "branches",
)

coverage.require_authoritative_complete(
    coverage.CoverageTotals(
        hit_lines=2,
        lines=2,
        hit_functions=1,
        functions=1,
        hit_branches=2,
        branches=2,
    )
)
