#!/usr/bin/env python3

"""Merge independently linked llvm-cov exports by source definition.

Each test executable must be reported against only the profile produced by that
executable.  This program unions those trustworthy, paired reports without
asking llvm-cov to match one executable's zero-hash inline functions to another
executable's profile record.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class SourceCoverage:
    """Accumulated coverage for one first-party source file."""

    lines: dict[int, int] = field(default_factory=lambda: defaultdict(int))
    functions: dict[tuple[tuple[int, ...], ...], tuple[str, int, int]] = field(
        default_factory=dict
    )
    branches: dict[tuple[int, ...], tuple[int, int]] = field(default_factory=dict)
    llvm_branches: dict[tuple[int, ...], tuple[int, int]] = field(default_factory=dict)
    folded_outcomes: set[tuple[tuple[int, ...], int]] = field(default_factory=set)
    ambiguous_fold_projection: bool = False


@dataclass(frozen=True)
class BranchProjection:
    """Exact LLVM branch totals plus their conservative LCOV projection."""

    branches: dict[tuple[int, ...], tuple[int, int]]
    llvm_branches: dict[tuple[int, ...], tuple[int, int]]
    folded_outcomes: frozenset[tuple[tuple[int, ...], int]]
    exact_covered: int
    exact_total: int
    ambiguous: bool


@dataclass(frozen=True)
class CanonicalFunction:
    """One C++ source definition with a stable LCOV identity."""

    lcov_name: str
    original_name: str
    count: int
    start_line: int


@dataclass(frozen=True)
class CoverageTotals:
    """Raw hit and denominator totals for the three reported metrics."""

    hit_lines: int = 0
    lines: int = 0
    hit_functions: int = 0
    functions: int = 0
    hit_branches: int = 0
    branches: int = 0

    def __add__(self, other: CoverageTotals) -> CoverageTotals:
        return CoverageTotals(
            self.hit_lines + other.hit_lines,
            self.lines + other.lines,
            self.hit_functions + other.hit_functions,
            self.functions + other.functions,
            self.hit_branches + other.hit_branches,
            self.branches + other.branches,
        )


def require_authoritative_complete(totals: CoverageTotals) -> None:
    """Fail unless every authoritative LLVM metric is completely covered."""

    incomplete = []
    for name, hit, defined in (
        ("lines", totals.hit_lines, totals.lines),
        ("functions", totals.hit_functions, totals.functions),
        ("branches", totals.hit_branches, totals.branches),
    ):
        if hit != defined:
            incomplete.append(f"{name} {hit}/{defined}")
    if incomplete:
        raise ValueError(
            "authoritative guarded LLVM coverage gate failed: "
            + ", ".join(incomplete)
        )


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--trace-dir", required=True, type=Path)
    parser.add_argument("--json-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--source-root", required=True, action="append", type=Path)
    parser.add_argument("--lcov-command", default="lcov")
    parser.add_argument("--summary-output", type=Path)
    return parser.parse_args()


def parse_lines(trace: Path) -> dict[str, dict[int, int]]:
    result: dict[str, dict[int, int]] = {}
    source: str | None = None
    with trace.open(encoding="utf-8") as stream:
        for raw_line in stream:
            line = raw_line.rstrip("\n")
            if line.startswith("SF:"):
                source = line[3:]
                result.setdefault(source, {})
            elif line.startswith("DA:"):
                if source is None:
                    raise ValueError(f"{trace}: DA record precedes SF record")
                fields = line[3:].split(",")
                line_number = int(fields[0])
                execution_count = int(fields[1])
                result[source][line_number] = max(
                    result[source].get(line_number, 0), execution_count
                )
    return result


def region_signature(
    function: dict[str, object], source_index: int
) -> tuple[tuple[int, ...], ...]:
    regions = function["regions"]
    if not isinstance(regions, list):
        raise ValueError("llvm-cov function regions must be a list")
    return tuple(
        tuple(int(value) for value in region[:4] + region[7:])
        for region in regions
        if int(region[5]) == source_index
    )


def canonical_functions(
    functions: dict[tuple[tuple[int, ...], ...], tuple[str, int, int]],
) -> list[CanonicalFunction]:
    """Coalesce mapping variants and create LCOV-unique definition names.

    Identical region signatures represent template instantiations of one source
    definition and are already coalesced by the input mapping.  A repeated
    mangled name represents the same C++ entity even if different linked
    executables retain different subsets of its coverage regions.
    """

    by_name: dict[
        str, list[tuple[tuple[tuple[int, ...], ...], int, int]]
    ] = defaultdict(list)
    for signature, (name, count, start_line) in functions.items():
        by_name[name].append((signature, count, start_line))

    result = []
    for name, variants in sorted(by_name.items()):
        signatures = sorted(signature for signature, _, _ in variants)
        encoded = json.dumps(signatures, separators=(",", ":"))
        digest = hashlib.sha256(encoded.encode("utf-8")).hexdigest()[:16]
        result.append(
            CanonicalFunction(
                lcov_name=f"{name}#rift-definition-{digest}",
                original_name=name,
                count=sum(count for _, count, _ in variants),
                start_line=min(start_line for _, _, start_line in variants),
            )
        )
    return sorted(result, key=lambda entry: (entry.start_line, entry.lcov_name))


def merge_line_counts(target: dict[int, int], incoming: dict[int, int]) -> None:
    """Union physical source lines and retain their aggregate execution count."""

    for line_number, count in incoming.items():
        target[line_number] = target.get(line_number, 0) + count


def branch_projection_for_source(
    functions: list[dict[str, object]],
    source: str,
    *,
    expected_total: int,
    expected_covered: int,
) -> BranchProjection:
    """Coalesce function-level LLVM branch records for one source file.

    LLVM 22 branch tuple elements 6, 7, and 8 are FileID, ExpandedFileID,
    and RegionKind.  A function can contain several authored branch regions at
    the same coordinates, so their occurrence ordinal is part of the source
    identity.  The ordinal resets for each function: aligned template
    instantiations then union componentwise while repeated regions in one
    function remain distinct.

    LLVM omits folded outcome identities from JSON, so the file summary alone
    supplies the exact non-folded cardinality. If more zero outcomes are
    candidates than the folded count, their identities are indeterminate. The
    LCOV projection then removes a stable number of complete affected
    decisions and is deliberately conservative; it never selects candidates
    to maximize represented coverage. LLVM's file summary can retain only one
    mapping variant at a source location, so its covered count is validated as
    a lower bound on the componentwise union of aligned template variants.
    Production report generation rejects an ambiguous projection while still
    exposing LLVM's exact canonical-source totals.
    """

    definitions: dict[tuple[int, ...], tuple[int, int]] = {}
    for function in functions:
        occurrences: dict[tuple[int, ...], int] = defaultdict(int)
        filenames = function["filenames"]
        for branch in function["branches"]:
            file_id = int(branch[6])
            if file_id >= len(filenames):
                raise ValueError("LLVM branch FileID is outside its filename table")
            if filenames[file_id] != source:
                continue
            base = tuple(int(value) for value in branch[:4] + branch[7:])
            ordinal = occurrences[base]
            occurrences[base] += 1
            signature = base + (ordinal,)
            previous = definitions.get(signature, (0, 0))
            definitions[signature] = (
                max(previous[0], int(branch[4])),
                max(previous[1], int(branch[5])),
            )

    outcomes = [
        (signature, outcome, count)
        for signature, counts in definitions.items()
        for outcome, count in enumerate(counts)
    ]
    folded_count = len(outcomes) - expected_total
    folded_candidates = sorted(
        (signature, outcome)
        for signature, outcome, count in outcomes
        if count == 0
    )
    if folded_count < 0 or folded_count > len(folded_candidates):
        raise ValueError(f"folded-branch total disagrees for {source}")
    ambiguous = 0 < folded_count < len(folded_candidates)
    folded = set(folded_candidates[:folded_count])

    result: dict[tuple[int, ...], tuple[int, int]] = {}
    for signature, outcome, count in outcomes:
        if (signature, outcome) in folded:
            continue
        counts = list(result.get(signature, (-1, -1)))
        counts[outcome] = count
        result[signature] = (counts[0], counts[1])

    total = sum(count >= 0 for counts in result.values() for count in counts)
    covered = sum(count > 0 for counts in result.values() for count in counts)
    if total != expected_total:
        raise ValueError(f"branch totals disagree for {source}")
    if covered < expected_covered:
        raise ValueError(f"covered-branch total disagrees for {source}")
    projected = {signature: counts for signature, counts in result.items() if min(counts) >= 0}
    return BranchProjection(
        branches=projected,
        llvm_branches=definitions,
        folded_outcomes=frozenset(folded),
        exact_covered=covered,
        exact_total=expected_total,
        ambiguous=ambiguous,
    )


def branches_for_source(
    functions: list[dict[str, object]],
    source: str,
    *,
    expected_total: int,
    expected_covered: int,
) -> dict[tuple[int, ...], tuple[int, int]]:
    """Return only the conservative LCOV branch projection for focused tests."""

    return branch_projection_for_source(
        functions,
        source,
        expected_total=expected_total,
        expected_covered=expected_covered,
    ).branches


def merge_export(
    trace: Path,
    export: Path,
    accumulated: dict[str, SourceCoverage],
    source_roots: tuple[Path, ...],
) -> None:
    line_data = parse_lines(trace)
    with export.open(encoding="utf-8") as stream:
        document = json.load(stream)
    if len(document["data"]) != 1:
        raise ValueError(f"{export}: expected exactly one llvm-cov data object")

    data = document["data"][0]
    file_documents = {
        entry["filename"]: entry
        for entry in data["files"]
        if any(
            Path(entry["filename"]).is_relative_to(source_root)
            for source_root in source_roots
        )
    }
    local_functions: dict[
        str, dict[tuple[tuple[int, ...], ...], tuple[str, int, int]]
    ] = defaultdict(dict)
    for function in data["functions"]:
        filenames = function["filenames"]
        for source_index, source in enumerate(filenames):
            if source not in file_documents:
                continue
            signature = region_signature(function, source_index)
            if not signature:
                continue
            name = str(function["name"])
            count = int(function["count"])
            start_line = min(region[0] for region in signature)
            old_name, old_count, old_line = local_functions[source].get(
                signature, (name, 0, start_line)
            )
            local_functions[source][signature] = (
                min(old_name, name),
                old_count + count,
                min(old_line, start_line),
            )
    for source, file_document in file_documents.items():
        summary = file_document["summary"]
        functions = local_functions[source]
        covered_functions = sum(count > 0 for _, count, _ in functions.values())
        if len(functions) != int(summary["functions"]["count"]):
            raise ValueError(f"{export}: function total disagrees for {source}")
        if covered_functions != int(summary["functions"]["covered"]):
            raise ValueError(f"{export}: covered-function total disagrees for {source}")

        projection = branch_projection_for_source(
            data["functions"],
            source,
            expected_total=int(summary["branches"]["count"]),
            expected_covered=int(summary["branches"]["covered"]),
        )

        target = accumulated.setdefault(source, SourceCoverage())
        if source not in line_data:
            raise ValueError(f"{trace}: no LCOV line record for {source}")
        merge_line_counts(target.lines, line_data[source])
        for signature, (name, count, start_line) in functions.items():
            old_name, old_count, old_line = target.functions.get(
                signature, (name, 0, start_line)
            )
            target.functions[signature] = (
                min(old_name, name),
                old_count + count,
                min(old_line, start_line),
            )
        for signature, counts in projection.branches.items():
            previous = target.branches.get(signature, (-1, -1))
            target.branches[signature] = (
                max(0, previous[0]) + counts[0] if counts[0] >= 0 else previous[0],
                max(0, previous[1]) + counts[1] if counts[1] >= 0 else previous[1],
            )
        for signature, counts in projection.llvm_branches.items():
            previous = target.llvm_branches.get(signature, (0, 0))
            target.llvm_branches[signature] = (previous[0] + counts[0], previous[1] + counts[1])
        target.folded_outcomes.update(projection.folded_outcomes)
        target.ambiguous_fold_projection = target.ambiguous_fold_projection or projection.ambiguous


def _record_totals(coverage: SourceCoverage) -> CoverageTotals:
    functions = canonical_functions(coverage.functions)
    branch_counts = [
        count for counts in coverage.branches.values() for count in counts if count >= 0
    ]
    return CoverageTotals(
        hit_lines=sum(count > 0 for count in coverage.lines.values()),
        lines=len(coverage.lines),
        hit_functions=sum(function.count > 0 for function in functions),
        functions=len(functions),
        hit_branches=sum(count > 0 for count in branch_counts),
        branches=len(branch_counts),
    )


def exact_llvm_totals(accumulated: dict[str, SourceCoverage]) -> CoverageTotals:
    """Return exact linked LLVM totals, including non-LCOV branch outcomes."""

    projected = CoverageTotals()
    hit_branches = 0
    branches = 0
    for source, coverage in accumulated.items():
        if coverage.ambiguous_fold_projection:
            raise ValueError(
                f"LLVM omitted an ambiguous folded-outcome identity for {source}"
            )
        projected += _record_totals(coverage)
        for signature, counts in coverage.llvm_branches.items():
            for outcome, count in enumerate(counts):
                if (signature, outcome) in coverage.folded_outcomes:
                    continue
                branches += 1
                hit_branches += count > 0
    return CoverageTotals(
        hit_lines=projected.hit_lines,
        lines=projected.lines,
        hit_functions=projected.hit_functions,
        functions=projected.functions,
        hit_branches=hit_branches,
        branches=branches,
    )


def write_lcov(
    output: Path, accumulated: dict[str, SourceCoverage]
) -> CoverageTotals:
    totals = CoverageTotals()
    with output.open("w", encoding="utf-8") as stream:
        for source, coverage in sorted(accumulated.items()):
            totals += _record_totals(coverage)
            stream.write(f"SF:{source}\n")
            functions = canonical_functions(coverage.functions)
            for function in functions:
                stream.write(f"FN:{function.start_line},{function.lcov_name}\n")
            for function in functions:
                stream.write(f"FNDA:{function.count},{function.lcov_name}\n")
            stream.write(f"FNF:{len(functions)}\n")
            stream.write(f"FNH:{sum(function.count > 0 for function in functions)}\n")

            for line_number, count in sorted(coverage.lines.items()):
                stream.write(f"DA:{line_number},{count}\n")

            branch_index = 0
            for signature, counts in sorted(coverage.branches.items()):
                line_number = signature[0]
                for count in counts:
                    if count < 0:
                        continue
                    stream.write(f"BRDA:{line_number},0,{branch_index},{count}\n")
                    branch_index += 1
            stream.write(f"BRF:{branch_index}\n")
            stream.write(
                "BRH:"
                f"{sum(count > 0 for pair in coverage.branches.values() for count in pair)}\n"
            )
            stream.write(f"LF:{len(coverage.lines)}\n")
            stream.write(f"LH:{sum(count > 0 for count in coverage.lines.values())}\n")
            stream.write("end_of_record\n")
    return totals


def parse_lcov_totals(report: Path) -> CoverageTotals:
    """Reparse an emitted LCOV file and reject inconsistent record metadata."""

    totals = CoverageTotals()
    record: dict[str, object] | None = None
    with report.open(encoding="utf-8") as stream:
        for raw_line in stream:
            line = raw_line.rstrip("\n")
            if line.startswith("SF:"):
                if record is not None:
                    raise ValueError("LCOV source record was not terminated")
                record = {
                    "fn": set(),
                    "fnda": {},
                    "da": {},
                    "brda": [],
                    "declared": {},
                }
            elif line == "end_of_record":
                if record is None:
                    raise ValueError("LCOV terminator precedes source record")
                function_names = record["fn"]
                function_counts = record["fnda"]
                lines = record["da"]
                branches = record["brda"]
                declared = record["declared"]
                if function_names != set(function_counts):
                    raise ValueError("LCOV function names and counts differ")
                actual = CoverageTotals(
                    hit_lines=sum(count > 0 for count in lines.values()),
                    lines=len(lines),
                    hit_functions=sum(count > 0 for count in function_counts.values()),
                    functions=len(function_names),
                    hit_branches=sum(count > 0 for count in branches),
                    branches=len(branches),
                )
                expected = CoverageTotals(
                    hit_lines=declared.get("LH", -1),
                    lines=declared.get("LF", -1),
                    hit_functions=declared.get("FNH", -1),
                    functions=declared.get("FNF", -1),
                    hit_branches=declared.get("BRH", -1),
                    branches=declared.get("BRF", -1),
                )
                if actual != expected:
                    raise ValueError(f"LCOV record totals differ: {actual} != {expected}")
                totals += actual
                record = None
            elif record is not None and line.startswith("FN:"):
                record["fn"].add(line.split(",", 1)[1])
            elif record is not None and line.startswith("FNDA:"):
                count, name = line[5:].split(",", 1)
                if name in record["fnda"]:
                    raise ValueError(f"duplicate LCOV function identity: {name}")
                record["fnda"][name] = int(count)
            elif record is not None and line.startswith("DA:"):
                line_number, count = line[3:].split(",", 2)[:2]
                record["da"][int(line_number)] = int(count)
            elif record is not None and line.startswith("BRDA:"):
                record["brda"].append(int(line.rsplit(",", 1)[1]))
            elif record is not None and line.startswith(
                ("FNF:", "FNH:", "BRF:", "BRH:", "LF:", "LH:")
            ):
                key, value = line.split(":", 1)
                record["declared"][key] = int(value)
    if record is not None:
        raise ValueError("LCOV source record was not terminated")
    return totals


def lcov_summary_totals(report: Path, lcov_command: str) -> tuple[CoverageTotals, str]:
    """Run LCOV and parse the three raw hit/denominator pairs it reports."""

    completed = subprocess.run(
        [lcov_command, "--summary", str(report), "--branch-coverage"],
        check=False,
        capture_output=True,
        text=True,
    )
    rendered = completed.stdout + completed.stderr
    if completed.returncode != 0:
        raise ValueError(f"lcov summary failed:\n{rendered}")

    pairs: dict[str, tuple[int, int]] = {}
    for metric in ("lines", "functions", "branches"):
        match = re.search(
            rf"{metric}\.*:\s+[^\n]*\((\d+) of (\d+) {metric}\)", rendered
        )
        if match is None:
            raise ValueError(f"lcov summary omitted {metric} totals:\n{rendered}")
        pairs[metric] = (int(match.group(1)), int(match.group(2)))
    return (
        CoverageTotals(
            hit_lines=pairs["lines"][0],
            lines=pairs["lines"][1],
            hit_functions=pairs["functions"][0],
            functions=pairs["functions"][1],
            hit_branches=pairs["branches"][0],
            branches=pairs["branches"][1],
        ),
        rendered,
    )


def main() -> None:
    arguments = parse_arguments()
    source_roots = tuple(path.resolve() for path in arguments.source_root)
    traces = sorted(arguments.trace_dir.glob("*.info"))
    exports = sorted(arguments.json_dir.glob("*.json"))
    if not traces or {path.stem for path in traces} != {path.stem for path in exports}:
        raise ValueError("LCOV and JSON exports do not pair exactly")

    accumulated: dict[str, SourceCoverage] = {}
    for export in exports:
        merge_export(
            arguments.trace_dir / f"{export.stem}.info",
            export,
            accumulated,
            source_roots,
        )
    internal_totals = write_lcov(arguments.output, accumulated)
    llvm_totals = exact_llvm_totals(accumulated)
    require_authoritative_complete(llvm_totals)
    parsed_totals = parse_lcov_totals(arguments.output)
    if parsed_totals != internal_totals:
        raise ValueError(
            f"emitted LCOV totals differ: {parsed_totals} != {internal_totals}"
        )
    summary_totals, rendered_summary = lcov_summary_totals(
        arguments.output, arguments.lcov_command
    )
    if summary_totals != internal_totals:
        raise ValueError(
            f"lcov summary totals differ: {summary_totals} != {internal_totals}"
        )
    if arguments.summary_output is not None:
        arguments.summary_output.write_text(
            "Authoritative guarded LLVM coverage gate: distinct physical source "
            "lines, canonical source definitions, and exact canonical authored "
            "branch outcomes\n"
            f"Exact guarded LLVM totals: lines {llvm_totals.hit_lines}/{llvm_totals.lines}, "
            f"functions {llvm_totals.hit_functions}/{llvm_totals.functions}, "
            f"branches {llvm_totals.hit_branches}/{llvm_totals.branches}\n"
            f"Conservative non-gating LCOV projection: lines {internal_totals.hit_lines}/{internal_totals.lines}, "
            f"functions {internal_totals.hit_functions}/{internal_totals.functions}, "
            f"branches {internal_totals.hit_branches}/{internal_totals.branches}\n"
            + rendered_summary,
            encoding="utf-8",
        )


if __name__ == "__main__":
    main()
