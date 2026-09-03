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
        --exclude-throw-branches \
        --exclude-unreachable-branches \
        --exclude-noncode-lines \
        --print-summary \
        --fail-under-line 100 \
        --fail-under-function 100 \
        --fail-under-branch 100 \
        --json-summary-pretty \
        --json-summary "${BUILD_DIR}/coverage-summary.json" \
        --cobertura-pretty \
        --output "${BUILD_DIR}/coverage.xml" \
        "${BUILD_DIR}"
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

# The same-order collective contract makes only this defensive Task 04 block
# unreachable. Exact locations prevent an unrelated future miss from passing.
readonly APPROVED_UNCOVERED_LINES=$'src/phase_graph.cpp:900\nsrc/phase_graph.cpp:901\nsrc/phase_graph.cpp:902\nsrc/phase_graph.cpp:903\nsrc/phase_graph.cpp:904'
readonly APPROVED_UNCOVERED_BRANCHES='src/phase_graph.cpp:898'

actual_uncovered_lines="$(
    awk \
        -v include_root="${REPOSITORY_ROOT}/include/rift/" \
        -v source_root="${REPOSITORY_ROOT}/src/" \
        -v repository_root="${REPOSITORY_ROOT}/" \
        '
        /^SF:/ {
            source = substr($0, 4)
            in_scope = index(source, include_root) == 1 || index(source, source_root) == 1
            relative_source = substr(source, length(repository_root) + 1)
            next
        }
        in_scope && /^DA:/ {
            split(substr($0, 4), fields, ",")
            if (fields[2] == 0) {
                print relative_source ":" fields[1]
            }
        }
        ' \
        "${COVERAGE_LCOV}" | sort -u
)"

actual_uncovered_branches="$(
    jq -r \
        --arg include_root "${REPOSITORY_ROOT}/include/rift/" \
        --arg source_root "${REPOSITORY_ROOT}/src/" \
        --arg repository_root "${REPOSITORY_ROOT}/" \
        '
        .data[].files[]
        | select((.filename | startswith($include_root)) or (.filename | startswith($source_root)))
        | .filename as $filename
        | .branches[]
        | select(.[4] == 0)
        | "\($filename | ltrimstr($repository_root)):\(.[0])"
        ' \
        "${COVERAGE_DATA}" | sort -u
)"

if ! jq -e \
    '.data[0].totals | (.lines.count - .lines.covered) == 5 and .functions.percent == 100 and .branches.notcovered == 1' \
    "${BUILD_DIR}/coverage-summary.json" >/dev/null || \
    [[ "${actual_uncovered_lines}" != "${APPROVED_UNCOVERED_LINES}" ]] || \
    [[ "${actual_uncovered_branches}" != "${APPROVED_UNCOVERED_BRANCHES}" ]]; then
    coverage_totals="$(jq -r \
        '.data[0].totals | "lines=\(.lines.percent)%, functions=\(.functions.percent)%, branches=\(.branches.percent)%"' \
        "${BUILD_DIR}/coverage-summary.json")"
    printf 'Unexpected uncovered Clang lines:\n%s\n' "${actual_uncovered_lines:-<none>}" >&2
    printf 'Unexpected uncovered Clang branches:\n%s\n' "${actual_uncovered_branches:-<none>}" >&2
    die "Clang coverage differs from the approved exclusions: ${coverage_totals}"
fi

printf 'Policy-adjusted Clang coverage:\n'
read -r adjusted_lines adjusted_functions adjusted_branches < <(
    jq -r '
        .data[0].totals
        | [
            .lines.covered,
            .functions.count,
            (.branches.count - .branches.notcovered)
          ]
        | @tsv
        ' \
        "${BUILD_DIR}/coverage-summary.json"
)
printf 'lines: 100.0%% (%s out of %s)\n' "${adjusted_lines}" "${adjusted_lines}"
printf 'functions: 100.0%% (%s out of %s)\n' "${adjusted_functions}" "${adjusted_functions}"
printf 'branches: 100.0%% (%s out of %s)\n' "${adjusted_branches}" "${adjusted_branches}"
