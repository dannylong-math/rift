#!/usr/bin/env python3

"""Verify deterministic raw gcovr missing-location inventories."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import gcovr_miss_inventory as inventory


report = {
    "gcovr/format_version": "0.6",
    "files": [
        {
            "file": "src/b.cpp",
            "functions": [
                {"name": "missing_b()", "lineno": 8, "execution_count": 0},
                {"name": "covered_b()", "lineno": 3, "execution_count": 1},
            ],
            "lines": [
                {
                    "line_number": 8,
                    "count": 0,
                    "branches": [
                        {"count": 0},
                        {"count": 2},
                        {"count": 0},
                    ],
                }
            ],
        },
        {
            "file": "include/a.hpp",
            "functions": [],
            "lines": [
                {
                    "line_number": 4,
                    "count": 1,
                    "branches": [{"count": 0}, {"count": 1}],
                }
            ],
        },
    ],
}

assert inventory.branch_rows(report) == [
    ("include/a.hpp", 4, 1, 2),
    ("src/b.cpp", 8, 2, 3),
]
assert inventory.line_rows(report) == [("src/b.cpp", 8, 0)]
assert inventory.function_rows(report) == [("src/b.cpp", 8, 0, "missing_b()")]
