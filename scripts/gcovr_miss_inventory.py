#!/usr/bin/env python3

"""Create deterministic TSV inventories from one strict gcovr JSON report."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

JsonReport = dict[str, Any]


def branch_rows(report: JsonReport) -> list[tuple[str, int, int, int]]:
    """Return source lines having at least one uncovered compiler-CFG arc."""

    rows: list[tuple[str, int, int, int]] = []
    for source in report["files"]:
        for line in source["lines"]:
            branches = line["branches"]
            uncovered = sum(branch["count"] == 0 for branch in branches)
            if uncovered:
                rows.append(
                    (source["file"], line["line_number"], uncovered, len(branches))
                )
    return sorted(rows)


def line_rows(report: JsonReport) -> list[tuple[str, int, int]]:
    """Return physical source lines whose gcov execution count is zero."""

    return sorted(
        (source["file"], line["line_number"], line["count"])
        for source in report["files"]
        for line in source["lines"]
        if line["count"] == 0
    )


def function_rows(report: JsonReport) -> list[tuple[str, int, int, str]]:
    """Return source definitions whose gcov execution count is zero."""

    return sorted(
        (
            source["file"],
            function["lineno"],
            function["execution_count"],
            function["name"],
        )
        for source in report["files"]
        for function in source["functions"]
        if function["execution_count"] == 0
    )


def write_tsv(path: Path, header: tuple[str, ...], rows: list[tuple[object, ...]]) -> None:
    """Write tab-separated rows using stable UTF-8 and Unix newlines."""

    text = "\t".join(header) + "\n"
    text += "".join("\t".join(str(field) for field in row) + "\n" for row in rows)
    path.write_text(text, encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    arguments = parser.parse_args()

    report = json.loads(arguments.report.read_text(encoding="utf-8"))
    arguments.output_dir.mkdir(parents=True, exist_ok=True)
    branches = branch_rows(report)
    lines = line_rows(report)
    functions = function_rows(report)
    write_tsv(
        arguments.output_dir / "raw-gcovr-branch-misses.tsv",
        ("file", "line", "uncovered", "total"),
        branches,
    )
    write_tsv(
        arguments.output_dir / "raw-gcovr-line-misses.tsv",
        ("file", "line", "count"),
        lines,
    )
    write_tsv(
        arguments.output_dir / "raw-gcovr-function-misses.tsv",
        ("file", "line", "count", "function"),
        functions,
    )
    print(
        f"branch_locations={len(branches)} "
        f"uncovered_branches={sum(row[2] for row in branches)} "
        f"line_records={len(lines)} function_records={len(functions)}"
    )


if __name__ == "__main__":
    main()
