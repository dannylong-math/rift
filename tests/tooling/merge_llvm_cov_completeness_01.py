#!/usr/bin/env python3

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import coverage_completeness_guard as guard


with tempfile.TemporaryDirectory() as temporary:
    manifest = Path(temporary) / "supported-templates.txt"
    omitted = "rift::SpaceRegistry<3>"
    manifest.write_text(
        "\n".join(sorted(guard.EXPECTED_TEMPLATE_USES - {omitted})) + "\n",
        encoding="utf-8",
    )

    try:
        guard.verify_template_manifest(manifest)
    except ValueError as error:
        assert omitted in str(error)
    else:
        raise AssertionError("an omitted supported template use was accepted")

    manifest.write_text(
        "\n".join(sorted(guard.EXPECTED_TEMPLATE_USES)) + "\n",
        encoding="utf-8",
    )
    guard.verify_template_manifest(manifest)
