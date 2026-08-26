#!/usr/bin/env python3

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import merge_llvm_cov_exports as coverage


lines = {4: 0, 5: 2}
coverage.merge_line_counts(lines, {4: 3, 6: 0})

assert lines == {4: 3, 5: 2, 6: 0}
