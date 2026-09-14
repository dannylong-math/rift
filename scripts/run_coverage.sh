#!/usr/bin/env bash

set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly REPOSITORY_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
readonly COVERAGE_KIND="${1:-}"
readonly COVERAGE_JOBS="${RIFT_COVERAGE_JOBS:-6}"

usage() {
    printf 'Usage: %s gcc|clang\n' "$0"
}

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

[[ "${COVERAGE_KIND}" =~ ^(gcc|clang)$ ]] || {
    usage >&2
    exit 2
}
[[ "${COVERAGE_JOBS}" =~ ^[1-9][0-9]*$ ]] ||
    die "RIFT_COVERAGE_JOBS must be a positive integer"

readonly PRESET="${COVERAGE_KIND}-coverage"
readonly BUILD_DIR="${REPOSITORY_ROOT}/build/${PRESET}"

if [[ "${COVERAGE_KIND}" == "gcc" ]]; then
    # Keep a user-installed PETSc/MPI stack from contaminating the isolated
    # system-OpenMPI dependency stack selected by the GCC coverage preset.
    unset CMAKE_PREFIX_PATH LD_LIBRARY_PATH PETSC_DIR PETSC_ARCH
fi

cd -- "${REPOSITORY_ROOT}"
cmake --preset "${PRESET}"
cmake --build --preset "${PRESET}" --parallel "${COVERAGE_JOBS}"

if [[ "${COVERAGE_KIND}" == "gcc" ]]; then
    readonly GCOVR="${REPOSITORY_ROOT}/.venv/bin/gcovr"
    [[ -x "${GCOVR}" ]] ||
        die "gcovr 8.6 is missing; install it with .venv/bin/python -m pip install gcovr==8.6"

    find "${BUILD_DIR}" -type f -name '*.gcda' -delete
    ctest --preset "${PRESET}" --output-on-failure

    printf 'Raw GCC coverage:\n'
    "${GCOVR}" \
        --root "${REPOSITORY_ROOT}" \
        --filter 'include/rift/' \
        --filter 'src/' \
        --print-summary \
        --json-summary-pretty \
        --output "${BUILD_DIR}/coverage-raw-summary.json" \
        "${BUILD_DIR}"

    printf 'Policy-adjusted GCC coverage:\n'
    "${GCOVR}" \
        --root "${REPOSITORY_ROOT}" \
        --filter 'include/rift/' \
        --filter 'src/' \
        --print-summary \
        --fail-under-line 100 \
        --fail-under-function 100 \
        --fail-under-branch 100 \
        --json-summary-pretty \
        --json-summary "${BUILD_DIR}/coverage-summary.json" \
        --cobertura-pretty \
        --output "${BUILD_DIR}/coverage.xml" \
        "${BUILD_DIR}"
    python3 - "${BUILD_DIR}/coverage-summary.json" <<'PY_COVERAGE'
import json
import sys

with open(sys.argv[1]) as report:
    summary = json.load(report)
if summary["line_total"] == 0 or summary["function_total"] == 0:
    sys.exit("error: GCC coverage contains no first-party executable lines or functions")
if summary["branch_total"] == 0:
    print("branches: not applicable (0 branches)")
PY_COVERAGE
    exit 0
fi

readonly PROFILE_DIR="${BUILD_DIR}/profiles"
cmake -E make_directory "${PROFILE_DIR}"
find "${PROFILE_DIR}" -type f -name '*.profraw' -delete
ctest --preset "${PRESET}" --output-on-failure

readonly CLANG_MAJOR="$(clang++ --version | sed -n 's/.*version \([0-9][0-9]*\).*/\1/p' | head -n 1)"

find_llvm_tool() {
    local tool_name="$1"
    local candidate
    for candidate in "${tool_name}-${CLANG_MAJOR}" "${tool_name}"; do
        if command -v "${candidate}" >/dev/null 2>&1; then
            command -v "${candidate}"
            return
        fi
    done
    die "could not find ${tool_name} matching clang++ ${CLANG_MAJOR}"
}

readonly LLVM_PROFDATA="$(find_llvm_tool llvm-profdata)"
readonly LLVM_COV="$(find_llvm_tool llvm-cov)"
readonly PROFILE_DATA="${BUILD_DIR}/coverage.profdata"
readonly COVERAGE_DATA="${BUILD_DIR}/coverage.json"
readonly COVERAGE_LCOV="${BUILD_DIR}/coverage.lcov"
command -v jq >/dev/null 2>&1 || die "jq is required to enforce Clang coverage thresholds"

shopt -s nullglob
profile_inputs=("${PROFILE_DIR}"/*.profraw)
(( ${#profile_inputs[@]} > 0 )) || die "Clang tests produced no raw coverage profiles"

test_objects=()
for test_source in \
    "${REPOSITORY_ROOT}"/tests/*_test.cpp \
    "${REPOSITORY_ROOT}"/tests/mpi/*_test.cpp; do
    test_object="${BUILD_DIR}/tests/$(basename "${test_source}" .cpp)"
    [[ -x "${test_object}" ]] || die "coverage test executable is missing: ${test_object}"
    test_objects+=("${test_object}")
done
(( ${#test_objects[@]} > 0 )) || die "no coverage test executables were found"

coverage_sources=(
    "${REPOSITORY_ROOT}"/include/rift/*.hpp
    "${REPOSITORY_ROOT}"/src/*.cpp
)
(( ${#coverage_sources[@]} > 0 )) || die "no first-party coverage sources were found"

"${LLVM_PROFDATA}" merge --sparse "${profile_inputs[@]}" --output "${PROFILE_DATA}"

object_arguments=("${test_objects[0]}")
for test_object in "${test_objects[@]:1}"; do
    object_arguments+=("--object=${test_object}")
done

printf 'Raw Clang coverage:\n'
"${LLVM_COV}" report \
    "${object_arguments[@]}" \
    --instr-profile "${PROFILE_DATA}" \
    --show-branch-summary \
    --sources "${coverage_sources[@]}"

"${LLVM_COV}" export \
    "${object_arguments[@]}" \
    --instr-profile "${PROFILE_DATA}" \
    --sources "${coverage_sources[@]}" \
    > "${COVERAGE_DATA}"

"${LLVM_COV}" export \
    "${object_arguments[@]}" \
    --instr-profile "${PROFILE_DATA}" \
    --format=lcov \
    --sources "${coverage_sources[@]}" \
    > "${COVERAGE_LCOV}"

jq '{type, version, data: [.data[] | {totals}]}' \
    "${COVERAGE_DATA}" > "${BUILD_DIR}/coverage-summary.json"

# There are no approved coverage exclusions in the reset library. Compare
# counts instead of percentages: a branch-free library has no missed branches,
# but reporting 100 percent branch coverage would hide the empty denominator.
if ! jq -e '
    (.data | length) > 0 and
    all(.data[].totals;
        .lines.count > 0 and .functions.count > 0 and
        .lines.covered == .lines.count and
        .functions.covered == .functions.count and
        .branches.covered == .branches.count)
    ' "${BUILD_DIR}/coverage-summary.json" >/dev/null; then
    die "Clang coverage must contain first-party lines/functions and no uncovered lines, functions, or branches"
fi

printf 'Policy-adjusted Clang coverage (no exclusions):\n'
jq -r '
    .data[].totals
    | "lines: 100.0% (\(.lines.covered) out of \(.lines.count))",
      "functions: 100.0% (\(.functions.covered) out of \(.functions.count))",
      (if .branches.count == 0 then "branches: not applicable (0 branches)"
       else "branches: 100.0% (\(.branches.covered) out of \(.branches.count))" end)
    ' "${BUILD_DIR}/coverage-summary.json"
