#!/usr/bin/env python3

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import merge_llvm_cov_exports as coverage


with tempfile.TemporaryDirectory() as directory:
    report = Path(directory) / "coverage.info"
    source = coverage.SourceCoverage()
    source.lines.update({2: 1, 3: 0})
    source.functions[((2, 1, 3, 2, 0),)] = ("function", 1, 2)
    source.branches[(3, 4, 3, 8, 4)] = (1, 0)
    expected = coverage.write_lcov(report, {"/project/src/example.cpp": source})
    assert coverage.parse_lcov_totals(report) == expected

    report.write_text(report.read_text().replace("FNF:1", "FNF:2"))
    try:
        coverage.parse_lcov_totals(report)
    except ValueError as error:
        assert "function" in str(error)
    else:
        raise AssertionError("inconsistent LCOV totals were accepted")
