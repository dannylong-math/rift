#!/usr/bin/env python3

"""Regress exact preservation of every pre-consolidation Boost.UT test name."""

from pathlib import Path

import subject_runner_inventory as inventory


ROOT = Path(__file__).resolve().parents[2]
LEGACY_PATH = Path(__file__).with_name("subject_runner_legacy_names.txt")
headers = sorted(
    path
    for path in (ROOT / "tests").rglob("*.hpp")
    if "register_tests" in path.read_text(encoding="utf-8")
)
current = inventory.test_names(headers)
legacy = inventory.legacy_names(LEGACY_PATH)
inventory.verify_legacy_names(current, legacy, 32)

rename_mutant = current.copy()
preserved = legacy[0]
rename_mutant[rename_mutant.index(preserved)] = preserved + " renamed mutant"
try:
    inventory.verify_legacy_names(rename_mutant, legacy, 32)
except AssertionError as error:
    assert preserved in str(error)
else:
    raise AssertionError("a renamed legacy Boost.UT test was accepted")

print("legacy Boost.UT names: 202 exact preserved names and 32 additions")
