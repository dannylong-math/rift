#!/usr/bin/env bash

set -euo pipefail

rift_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage()
{
    cat <<EOF
Usage: $0 [coverage-build-directory]

Generate Rift's authoritative guarded exact LLVM coverage report. The report
gates distinct physical lines, canonical source definitions, and exact
canonical authored branch outcomes. A conservative non-gating LCOV projection
is emitted separately for genhtml. The command exits nonzero if either
completeness guard fails or any authoritative metric is below 100 percent.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

build_argument="${1:-${rift_root}/build/coverage-clang}"

if [[ ! -d "${build_argument}" ]]; then
    echo "coverage build directory does not exist: ${build_argument}" >&2
    echo "configure it with: cmake --preset coverage-clang" >&2
    exit 2
fi
build_dir="$(cd "${build_argument}" && pwd)"

llvm_profdata="${LLVM_PROFDATA:-llvm-profdata-22}"
llvm_cov="${LLVM_COV:-llvm-cov-22}"
lcov_tool="${LCOV:-lcov}"
genhtml_tool="${GENHTML:-genhtml}"
profile_dir="$(mktemp -d "${build_dir}/coverage-profiles.XXXXXX")"
report_dir="${profile_dir}/report"
raw_dir="${profile_dir}/raw"
profiles_dir="${profile_dir}/profiles"
exports_dir="${profile_dir}/exports"
traces_dir="${profile_dir}/traces"
mkdir -p "${report_dir}" "${raw_dir}" "${profiles_dir}" "${exports_dir}" "${traces_dir}"

object_manifest="${profile_dir}/test-executables.list0"
"${rift_root}/scripts/coverage_test_manifest.py" --build-dir "${build_dir}" \
    > "${object_manifest}"
mapfile -d '' objects < "${object_manifest}"
test_pair_manifest="${profile_dir}/test-executable-ctests.list0"
"${rift_root}/scripts/coverage_test_manifest.py" --build-dir "${build_dir}" \
    --emit-test-pairs > "${test_pair_manifest}"
mapfile -d '' test_pairs < "${test_pair_manifest}"

mapfile -d '' sources < <(
    find "${rift_root}/include/rift" "${rift_root}/src" -type f \
        \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' \) -print0 | sort -z
)

for object in "${objects[@]}"; do
    test_name="$(basename "${object}")"
    runner_arguments=()
    for ((pair_index = 0; pair_index < ${#test_pairs[@]}; pair_index += 2)); do
        if [[ "${test_pairs[pair_index]}" == "${object}" ]]; then
            runner_arguments+=(--test-name "${test_pairs[pair_index + 1]}")
        fi
    done
    "${rift_root}/scripts/coverage_test_runner.py" \
        --build-dir "${build_dir}" \
        --executable "${object}" \
        --raw-dir "${raw_dir}" \
        "${runner_arguments[@]}"

    mapfile -d '' profiles < <(
        find "${raw_dir}" -maxdepth 1 -type f -name "${test_name}-*.profraw" -print0 |
            sort -z
    )
    if (( ${#profiles[@]} == 0 )); then
        echo "no raw Clang profile was generated for ${test_name}" >&2
        exit 5
    fi
    "${llvm_profdata}" merge -sparse "${profiles[@]}" \
        -o "${profiles_dir}/${test_name}.profdata"

    error_file="${exports_dir}/${test_name}.err"
    "${llvm_cov}" export "${object}" \
        -instr-profile="${profiles_dir}/${test_name}.profdata" \
        -format=lcov \
        -sources "${sources[@]}" \
        > "${traces_dir}/${test_name}.info" 2> "${error_file}"
    if [[ -s "${error_file}" ]]; then
        echo "llvm-cov emitted diagnostics for ${test_name}:" >&2
        cat "${error_file}" >&2
        exit 6
    fi

    "${llvm_cov}" export "${object}" \
        -instr-profile="${profiles_dir}/${test_name}.profdata" \
        -format=text \
        -sources "${sources[@]}" \
        > "${exports_dir}/${test_name}.json" 2> "${error_file}"
    if [[ -s "${error_file}" ]]; then
        echo "llvm-cov emitted diagnostics for ${test_name}:" >&2
        cat "${error_file}" >&2
        exit 7
    fi
done

"${rift_root}/scripts/coverage_completeness_guard.py" \
    --source-root "${rift_root}" \
    --guard-export "${exports_dir}/coverage_completeness_run_tests.json" \
    --template-manifest "${rift_root}/tests/coverage/supported_templates.txt" \
    --guard-source "${rift_root}/tests/coverage/coverage_completeness_guard_00.hpp"

"${rift_root}/scripts/merge_llvm_cov_exports.py" \
    --trace-dir "${traces_dir}" \
    --json-dir "${exports_dir}" \
    --source-root "${rift_root}/include/rift" \
    --source-root "${rift_root}/src" \
    --lcov-command "${lcov_tool}" \
    --summary-output "${report_dir}/raw-summary.txt" \
    --output "${report_dir}/rift.info"
"${genhtml_tool}" --branch-coverage --output-directory "${report_dir}/html" \
    "${report_dir}/rift.info"

cat "${report_dir}/raw-summary.txt"
echo "Authoritative guarded LLVM coverage report: ${report_dir}/raw-summary.txt"
echo "Conservative LCOV/genhtml projection: ${report_dir}/html/index.html"
echo "Raw profile and summary directory: ${report_dir}"
