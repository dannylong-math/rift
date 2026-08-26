#!/usr/bin/env python3

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import coverage_completeness_guard as guard


with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    manifest = root / "supported-templates.txt"
    source = root / "coverage-guard.hpp"
    entries = sorted(guard.EXPECTED_TEMPLATE_USES)
    manifest.write_text("\n".join(entries) + "\n", encoding="utf-8")

    def structural_use(entry: str) -> str:
        family = entry.split("<", maxsplit=1)[0]
        if family in guard.CLASS_TEMPLATE_FAMILIES:
            return f"template class {entry};"
        if family == "rift::make_mesh_snapshot":
            return f"template void {entry}();"
        if family in guard.ALIAS_TEMPLATE_FAMILIES:
            return f"static_assert(std::same_as<{entry}, void>);"
        raise AssertionError(f"unreviewed fixture entry: {entry}")

    declarations = [structural_use(entry) for entry in entries]
    source.write_text("\n".join(declarations) + "\n", encoding="utf-8")
    guard.verify_guard_source_template_uses(manifest, source)

    omitted = entries[-1]
    source.write_text("\n".join(declarations[:-1]) + "\n", encoding="utf-8")
    try:
        guard.verify_guard_source_template_uses(manifest, source)
    except ValueError as error:
        assert omitted in str(error)
    else:
        raise AssertionError("an unreferenced supported template entry was accepted")

    source.write_text(
        "\n".join(declarations[:-1] + [f"// {declarations[-1]}"]) + "\n",
        encoding="utf-8",
    )
    try:
        guard.verify_guard_source_template_uses(manifest, source)
    except ValueError as error:
        assert omitted in str(error)
    else:
        raise AssertionError("a comment-only template-use mutant was accepted")

    source.write_text(
        "\n".join(
            declarations[:-1]
            + [f'constexpr auto ignored = "{declarations[-1]}";']
        )
        + "\n",
        encoding="utf-8",
    )
    try:
        guard.verify_guard_source_template_uses(manifest, source)
    except ValueError as error:
        assert omitted in str(error)
    else:
        raise AssertionError("a string-literal template-use mutant was accepted")

    source.write_text(
        "\n".join(
            declarations[:-1] + ["#if 0", declarations[-1], "#endif"]
        )
        + "\n",
        encoding="utf-8",
    )
    try:
        guard.verify_guard_source_template_uses(manifest, source)
    except ValueError as error:
        assert omitted in str(error)
    else:
        raise AssertionError("an inactive-preprocessor template-use mutant was accepted")

    source.write_text("\n".join(entries) + "\n", encoding="utf-8")
    try:
        guard.verify_guard_source_template_uses(manifest, source)
    except ValueError:
        pass
    else:
        raise AssertionError("unstructured template-name substrings were accepted")
