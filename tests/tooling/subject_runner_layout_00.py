#!/usr/bin/env python3

"""Enforce the reviewed subject-runner source and registration contract."""

from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path

import subject_runner_inventory as inventory


ROOT = Path(__file__).resolve().parents[2]
MATRIX_PATH = Path(__file__).with_name("subject_runner_matrix.json")
MATRIX = json.loads(MATRIX_PATH.read_text(encoding="utf-8"))


def fail(message: str) -> None:
    raise AssertionError(message)


runners = {ROOT / path: suite for path, suite in MATRIX["runners"].items()}
allowed_cpp = set(runners)
actual_cpp = set((ROOT / "tests").glob("*.cpp"))
actual_cpp.update((ROOT / "tests" / "mpi").glob("*.cpp"))
actual_cpp.update((ROOT / "tests" / "coverage").glob("*.cpp"))
if actual_cpp != allowed_cpp:
    fail(
        "test executable source allowlist mismatch: "
        f"missing={sorted(map(str, allowed_cpp - actual_cpp))}, "
        f"extra={sorted(map(str, actual_cpp - allowed_cpp))}"
    )

registration_headers: set[Path] = set()
registration_sources: set[Path] = set()
for entry in MATRIX["registrations"]:
    source = ROOT / entry["source"]
    header = ROOT / entry["header"]
    runner = ROOT / entry["runner"]
    if source.with_suffix(".hpp") != header:
        fail(f"registration source/header path mismatch: {source} -> {header}")
    if source.exists() and source != header:
        fail(f"retired source still exists: {source}")
    if not header.is_file():
        fail(f"registration header is missing: {header}")
    if runner not in runners:
        fail(f"registration maps to an unreviewed runner: {runner}")
    if entry["suite"] != runners[runner]:
        fail(f"registration suite/runner mismatch: {header}")
    if source.resolve() in registration_sources:
        fail(f"registration source is mapped more than once: {source}")
    if header.resolve() in registration_headers:
        fail(f"registration header is mapped more than once: {header}")
    registration_sources.add(source.resolve())
    registration_headers.add(header.resolve())

discovered_headers = {
    path.resolve()
    for path in (ROOT / "tests").rglob("*.hpp")
    if re.search(r"\bvoid\s+register_tests\s*\(", path.read_text(encoding="utf-8"))
}
if registration_headers != discovered_headers:
    fail(
        "reviewed registration manifest mismatch: "
        f"missing={sorted(map(str, discovered_headers - registration_headers))}, "
        f"extra={sorted(map(str, registration_headers - discovered_headers))}"
    )

include_counts: Counter[Path] = Counter()
call_counts: Counter[str] = Counter()
all_test_names: list[str] = []
expect_sites = 0
for runner, suite in runners.items():
    text = runner.read_text(encoding="utf-8")
    suite_pattern = rf'suite<"{re.escape(suite)}">\s+const'
    if len(re.findall(suite_pattern, text)) != 1:
        fail(f"runner must define exactly one automatic {suite!r} suite: {runner}")
    if "static " in text and "suite<" in text:
        fail(f"runner suite may not have static storage: {runner}")
    if not re.search(rf'suite<"{re.escape(suite)}">\s+const\s+\w+\s*=\s*\[\]', text):
        fail(f"runner suite lambda must be captureless: {runner}")
    if "invalid_test_mode" not in text:
        fail(f"runner lacks fail-closed mode dispatch: {runner}")
    for include in re.findall(r'^#include\s+"([^"]+\.hpp)"', text, re.MULTILINE):
        included = (runner.parent / include).resolve()
        if included in discovered_headers:
            include_counts[included] += 1
    for namespace in re.findall(
        r"rift_test::((?:mpi::)?[A-Za-z0-9_]+)::register_tests\b", text
    ):
        call_counts[namespace] += 1

for header in discovered_headers:
    text = header.read_text(encoding="utf-8")
    stem = header.stem
    namespace = f"mpi::{stem}" if header.parent.name == "mpi" else stem
    if not text.startswith("#pragma once"):
        fail(f"registration header must begin with #pragma once: {header}")
    if not re.search(rf"namespace\s+rift_test::{re.escape(namespace)}\s*\{{", text):
        fail(f"registration header lacks its unique rift_test namespace: {header}")
    if re.search(r"^namespace\s*\{", text, re.MULTILINE):
        fail(f"anonymous namespace is forbidden in an included registration unit: {header}")
    if "suite<" in text:
        fail(f"registration header may not declare a suite: {header}")
    if not re.search(r"\binline\s+void\s+register_tests\s*\(", text):
        fail(f"registration function defined in a header must be inline: {header}")
    if include_counts[header] != 1:
        fail(f"registration header must be included by exactly one runner: {header}")
    if call_counts[namespace] != 1:
        fail(f"registration function must be called exactly once in runner source: {header}")
    names = inventory.TEST_NAME_PATTERN.findall(text)
    all_test_names.extend(names)
    expect_sites += len(re.findall(r"\bexpect\s*\(", text))

duplicates = sorted(name for name, count in Counter(all_test_names).items() if count != 1)
if duplicates:
    fail(f"Boost.UT test names must be globally unique: {duplicates}")
if len(all_test_names) < MATRIX["minimum_named_tests"]:
    fail(
        f"named-test inventory regressed: {len(all_test_names)} < "
        f"{MATRIX['minimum_named_tests']}"
    )
if expect_sites < MATRIX["minimum_expect_sites"]:
    fail(
        f"expect-site inventory regressed: {expect_sites} < "
        f"{MATRIX['minimum_expect_sites']}"
    )

legacy = inventory.legacy_names(
    Path(__file__).with_name("subject_runner_legacy_names.txt")
)
inventory.verify_legacy_names(
    all_test_names, legacy, MATRIX["expected_additional_named_tests"]
)

for entry in MATRIX["registrations"]:
    runner_text = (ROOT / entry["runner"]).read_text(encoding="utf-8")
    selectors = entry["selector"].split(",")
    ranks = entry["ranks"]
    if len(selectors) != len(ranks):
        fail(f"registration selector/rank arity mismatch: {entry['header']}")
    target = Path(entry["runner"]).stem
    for selector, rank in zip(selectors, ranks, strict=True):
        if f'"{selector}"' not in runner_text:
            fail(f"runner omits reviewed selector {selector!r}: {entry['runner']}")
        aliases = [
            alias
            for alias in MATRIX["ctests"]
            if alias["target"] == target
            and alias["mode"] == selector
            and alias["processors"] == rank
        ]
        if len(aliases) != 1:
            fail(
                "registration selector/rank lacks one golden CTest alias: "
                f"{entry['header']} selector={selector!r} rank={rank}"
            )
        if bool(aliases[0].get("fatal")) != bool(entry.get("fatal")):
            fail(f"registration fatal/nonfatal mapping mismatch: {entry['header']}")

print(
    "subject-runner layout: "
    f"{len(runners)} runners, {len(discovered_headers)} registration headers, "
    f"{len(all_test_names)} named tests, {expect_sites} expect sites"
)
