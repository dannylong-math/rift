#!/usr/bin/env python3

"""Reject incomplete LLVM coverage object and supported-template manifests."""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
from pathlib import Path


TEMPLATE_FAMILIES = (
    "rift::MeshSnapshot",
    "rift::make_mesh_snapshot",
    "rift::FieldGroupSpace",
    "rift::LevelSetFieldSpace",
    "rift::SpaceDraft",
    "rift::SpaceSnapshot",
    "rift::SpaceRegistry",
    "rift::SpaceDraftResult",
    "rift::SpaceSnapshotResult",
)

EXPECTED_TEMPLATE_USES = {
    f"{family}<{dimension}>"
    for family in TEMPLATE_FAMILIES
    for dimension in (2, 3)
}

CLASS_TEMPLATE_FAMILIES = (
    "rift::MeshSnapshot",
    "rift::FieldGroupSpace",
    "rift::LevelSetFieldSpace",
    "rift::SpaceDraft",
    "rift::SpaceSnapshot",
    "rift::SpaceRegistry",
)
ALIAS_TEMPLATE_FAMILIES = (
    "rift::SpaceDraftResult",
    "rift::SpaceSnapshotResult",
)


def _manifest_entries(path: Path) -> set[str]:
    return {
        line.strip()
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }


def verify_template_manifest(path: Path) -> None:
    """Require an exact reviewed entry for every supported template and dimension."""
    actual = _manifest_entries(path)
    missing = sorted(EXPECTED_TEMPLATE_USES - actual)
    extra = sorted(actual - EXPECTED_TEMPLATE_USES)
    if missing or extra:
        raise ValueError(
            "supported-template manifest mismatch: "
            f"missing={missing}, extra={extra}"
        )


def _preprocessed_contract_source(source: str) -> str:
    """Preprocess declarations and erase literals that cannot instantiate code."""
    without_includes = re.sub(
        r"(?m)^\s*#\s*(?:include|pragma)\b[^\n]*$", "", source
    )
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    completed = subprocess.run(
        [*compiler, "-E", "-P", "-x", "c++", "-"],
        input=without_includes,
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise ValueError(
            "coverage guard preprocessing failed: " + completed.stderr.strip()
        )
    preprocessed = completed.stdout
    without_raw_literals = re.sub(
        r'(?:u8|u|U|L)?R"([^\s()\\]{0,16})\(.*?\)\1"',
        "",
        preprocessed,
        flags=re.DOTALL,
    )
    return re.sub(
        r'(?:u8|u|U|L)?"(?:\\.|[^"\\])*"|(?:u8|u|U|L)?\'(?:\\.|[^\'\\])*\'',
        "",
        without_raw_literals,
        flags=re.DOTALL,
    )


def verify_guard_source_template_uses(manifest: Path, guard_source: Path) -> None:
    """Tie the manifest to declarations that the compiler must instantiate/check."""
    entries = _manifest_entries(manifest)
    source = _preprocessed_contract_source(
        guard_source.read_text(encoding="utf-8")
    )
    class_families = "|".join(map(re.escape, CLASS_TEMPLATE_FAMILIES))
    alias_families = "|".join(map(re.escape, ALIAS_TEMPLATE_FAMILIES))
    explicit_classes = {
        re.sub(r"\s+", "", match)
        for match in re.findall(
            rf"\btemplate\s+class\s+((?:{class_families})\s*<\s*[23]\s*>)\s*;",
            source,
        )
    }
    explicit_factories = {
        re.sub(r"\s+", "", match)
        for match in re.findall(
            r"\btemplate\b[^;]*?\b(rift::make_mesh_snapshot\s*<\s*[23]\s*>)\s*\([^;]*\)\s*;",
            source,
            flags=re.DOTALL,
        )
    }
    checked_aliases = {
        re.sub(r"\s+", "", match)
        for match in re.findall(
            rf"\bstatic_assert\s*\(\s*std::same_as\s*<\s*((?:{alias_families})\s*<\s*[23]\s*>)\s*,",
            source,
        )
    }
    structural_uses = explicit_classes | explicit_factories | checked_aliases
    missing = sorted(entries - structural_uses)
    extra = sorted(structural_uses - entries)
    if missing or extra:
        raise ValueError(
            "coverage guard structural template-use mismatch: "
            f"missing={missing}, extra={extra}"
        )


def _exported_files(path: Path) -> set[Path]:
    document = json.loads(path.read_text(encoding="utf-8"))
    return {
        Path(file_record["filename"]).resolve()
        for export in document.get("data", [])
        for file_record in export.get("files", [])
    }


def verify_production_units(source_root: Path, guard_export: Path) -> None:
    """Require every current ``src/*.cpp`` unit in the whole-archive guard export."""
    expected = {path.resolve() for path in (source_root / "src").glob("*.cpp")}
    exported = _exported_files(guard_export)
    missing = sorted(expected - exported)
    if missing:
        relative = [str(path.relative_to(source_root.resolve())) for path in missing]
        raise ValueError(
            "whole-archive coverage export omitted production units: "
            f"{relative}"
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--guard-export", required=True, type=Path)
    parser.add_argument("--template-manifest", required=True, type=Path)
    parser.add_argument("--guard-source", required=True, type=Path)
    arguments = parser.parse_args()

    verify_template_manifest(arguments.template_manifest)
    verify_guard_source_template_uses(
        arguments.template_manifest, arguments.guard_source
    )
    verify_production_units(arguments.source_root, arguments.guard_export)
    print(
        "coverage completeness guard: "
        f"{len(EXPECTED_TEMPLATE_USES)} supported template uses and "
        f"{len(list((arguments.source_root / 'src').glob('*.cpp')))} production units"
    )


if __name__ == "__main__":
    main()
