#!/usr/bin/env python3

import argparse
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import merge_llvm_cov_exports as coverage


complete = coverage.CoverageTotals(
    hit_lines=2,
    lines=2,
    hit_functions=1,
    functions=1,
    hit_branches=2,
    branches=2,
)

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    traces = root / "traces"
    exports = root / "exports"
    traces.mkdir()
    exports.mkdir()
    (traces / "fixture.info").touch()
    (exports / "fixture.json").touch()
    arguments = argparse.Namespace(
        trace_dir=traces,
        json_dir=exports,
        output=root / "result.info",
        source_root=[root],
        lcov_command="lcov",
        summary_output=root / "summary.txt",
    )
    coverage.parse_arguments = lambda: arguments
    coverage.merge_export = lambda *args: None
    coverage.write_lcov = lambda *args: complete
    coverage.parse_lcov_totals = lambda *args: complete
    coverage.lcov_summary_totals = lambda *args: (complete, "summary\n")

    for totals, metric in (
        (coverage.CoverageTotals(1, 2, 1, 1, 2, 2), "lines"),
        (coverage.CoverageTotals(2, 2, 0, 1, 2, 2), "functions"),
        (coverage.CoverageTotals(2, 2, 1, 1, 1, 2), "branches"),
    ):
        coverage.exact_llvm_totals = lambda accumulated, value=totals: value
        try:
            coverage.main()
        except ValueError as error:
            assert metric in str(error)
        else:
            raise AssertionError(f"merger main accepted uncovered {metric}")
