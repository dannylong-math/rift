#!/usr/bin/env python3

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import merge_llvm_cov_exports as coverage


functions = {
    ((10, 2, 10, 8, 0),): ("same_mangled_name", 2, 10),
    ((11, 2, 11, 8, 0),): ("same_mangled_name", 3, 11),
    ((20, 2, 20, 8, 0),): ("other_mangled_name", 1, 20),
}
canonical = coverage.canonical_functions(functions)
by_name = {function.original_name: function for function in canonical}

assert len(canonical) == 2
assert by_name["same_mangled_name"].count == 5
assert by_name["same_mangled_name"].lcov_name.startswith(
    "same_mangled_name#rift-definition-"
)
assert len({function.lcov_name for function in canonical}) == 2
