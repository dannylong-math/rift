#!/usr/bin/env python3

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import merge_llvm_cov_exports as coverage


source = "/project/include/rift/template.hpp"


def template_function(name, counts):
    return {
        "name": name,
        "filenames": [source],
        "regions": [[1, 1, 40, 2, 1, 0, 0, 0]],
        "branches": [
            [10, 3, 10, 8, true_count, false_count, 0, 0, 4]
            for true_count, false_count in counts
        ],
    }


functions = [template_function("field_space<2>", [(1, 0), (1, 0), (1, 0)])]

forward = coverage.branch_projection_for_source(
    functions, source, expected_total=5, expected_covered=3
)

assert forward.ambiguous
assert forward.exact_total == 5
assert forward.exact_covered == 3
assert len(forward.branches) == 2
assert sum(count >= 0 for counts in forward.branches.values() for count in counts) == 4
assert sum(count > 0 for counts in forward.branches.values() for count in counts) == 2

accumulated = {
    source: coverage.SourceCoverage(
        llvm_branches=forward.llvm_branches,
        folded_outcomes=set(forward.folded_outcomes),
        ambiguous_fold_projection=forward.ambiguous,
    )
}
try:
    coverage.exact_llvm_totals(accumulated)
except ValueError as error:
    assert "ambiguous folded-outcome identity" in str(error)
else:
    raise AssertionError("ambiguous folded-outcome identity was accepted")
