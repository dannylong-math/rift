#!/usr/bin/env python3

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import merge_llvm_cov_exports as coverage


source = "/project/include/rift/template.hpp"
functions = [
    {
        "name": "field_space<2>",
        "filenames": [source],
        "branches": [[10, 3, 10, 8, 1, 0, 0, 0, 4]],
    },
    {
        "name": "field_space<3>",
        "filenames": [source],
        "branches": [[10, 3, 10, 8, 0, 2, 0, 0, 4]],
    },
]

projection = coverage.branch_projection_for_source(
    functions, source, expected_total=2, expected_covered=1
)

assert projection.exact_total == 2
assert projection.exact_covered == 2
assert len(projection.branches) == 1
assert list(projection.branches.values()) == [(1, 2)]
