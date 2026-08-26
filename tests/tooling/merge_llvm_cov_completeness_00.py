#!/usr/bin/env python3

import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import coverage_completeness_guard as guard


with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    source = root / "src"
    source.mkdir()
    first = source / "first.cpp"
    second = source / "second.cpp"
    first.write_text("int first();\n", encoding="utf-8")
    second.write_text("int second();\n", encoding="utf-8")
    export = root / "guard.json"
    export.write_text(
        json.dumps({"data": [{"files": [{"filename": str(first.resolve())}]}]}),
        encoding="utf-8",
    )

    try:
        guard.verify_production_units(root, export)
    except ValueError as error:
        assert "second.cpp" in str(error)
    else:
        raise AssertionError("an omitted production object was accepted")

    document = json.loads(export.read_text(encoding="utf-8"))
    document["data"][0]["files"].append({"filename": str(second.resolve())})
    export.write_text(json.dumps(document), encoding="utf-8")
    guard.verify_production_units(root, export)
