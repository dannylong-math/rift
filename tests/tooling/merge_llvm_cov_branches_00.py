#!/usr/bin/env python3

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import merge_llvm_cov_exports as coverage


source = "/project/include/rift/example.hpp"
functions = [
    {
        "filenames": ["/dependency.hpp", source],
        "branches": [[10, 3, 10, 8, 0, 4, 1, 0, 4]],
    }
]
projection = coverage.branch_projection_for_source(
    functions, source, expected_total=1, expected_covered=1
)

assert not projection.ambiguous
assert projection.exact_total == 1
assert projection.exact_covered == 1
assert projection.branches == {}

accumulated = {
    source: coverage.SourceCoverage(
        llvm_branches=projection.llvm_branches,
        folded_outcomes=set(projection.folded_outcomes),
    )
}
totals = coverage.exact_llvm_totals(accumulated)
assert totals.branches == 1
assert totals.hit_branches == 1
