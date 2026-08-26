"""Shared exact-name inventory checks for consolidated Boost.UT registrations."""

from __future__ import annotations

import re
from collections import Counter
from pathlib import Path


TEST_NAME_PATTERN = re.compile(r'"([^"\\]*(?:\\.[^"\\]*)*)"_test')


def test_names(headers: list[Path]) -> list[str]:
    return [
        name
        for header in headers
        for name in TEST_NAME_PATTERN.findall(header.read_text(encoding="utf-8"))
    ]


def legacy_names(path: Path) -> list[str]:
    return [
        line.strip()
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]


def verify_legacy_names(
    current: list[str], legacy: list[str], expected_additions: int
) -> None:
    legacy_counts = Counter(legacy)
    if len(legacy) != 202 or any(count != 1 for count in legacy_counts.values()):
        raise AssertionError("legacy Boost.UT inventory must contain 202 unique names")

    current_counts = Counter(current)
    renamed_or_duplicated = sorted(
        name for name in legacy if current_counts[name] != 1
    )
    if renamed_or_duplicated:
        raise AssertionError(
            "legacy Boost.UT names must each occur exactly once: "
            f"{renamed_or_duplicated}"
        )
    duplicate_current = sorted(
        name for name, count in current_counts.items() if count != 1
    )
    if duplicate_current:
        raise AssertionError(f"Boost.UT test names must be unique: {duplicate_current}")
    additions = set(current_counts) - set(legacy_counts)
    if len(additions) != expected_additions:
        raise AssertionError(
            "additive Boost.UT inventory mismatch: "
            f"{len(additions)} != {expected_additions}"
        )
