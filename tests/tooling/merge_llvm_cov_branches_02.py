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
        "branches": [[10, 3, 10, 8, 8, 2, 0, 0, 4]],
    },
    {
        "name": "field_space<3>",
        "filenames": [source],
        "branches": [[10, 3, 10, 8, 12, 2, 0, 0, 4]],
    },
]

branches = coverage.branches_for_source(
    functions, source, expected_total=2, expected_covered=2
)

assert list(branches.values()) == [(12, 2)]
