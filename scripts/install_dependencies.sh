#!/usr/bin/env bash

set -euo pipefail

readonly DEALII_VERSION="9.8.0"
readonly DEALII_SHA256="d8d66aac57baad145a752d3f11cf72cfa9457e3f99ae09e5c8d5c9259a83aee1"
readonly P4EST_VERSION="2.8.7"
readonly P4EST_SHA256="0a1e912f3529999ca6d62fee335d51f24b5650b586e95a03ef39ebf73936d7f4"
readonly ZLIB_VERSION="1.3.1"
readonly ZLIB_SHA256="9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

DEPENDENCY_ROOT="${REPOSITORY_ROOT}/.dependencies"
INSTALL_SCIENCE=true
INSTALL_DOCS=true
CHECK_ONLY=false
JOBS=""

usage() {
    printf '%s\n' \
        "Usage: $0 [options]" \
        "" \
        "Build Rift's dependencies without sudo or system-wide changes." \
        "" \
        "Options:" \
        "  --prefix PATH       Dependency root (default: .dependencies)" \
        "  --jobs N            Parallel jobs (default: CPU count, capped at 8)" \
        "  --docs-only         Install only the Python documentation tools" \
        "  --science-only      Install only zlib, p4est, and deal.II" \
        "  --check             Check host prerequisites without installing" \
        "  -h, --help          Show this help"
}

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

while (($# > 0)); do
    case "$1" in
        --prefix)
            (($# >= 2)) || die "--prefix requires a path"
            DEPENDENCY_ROOT="$2"
            shift 2
            ;;
        --jobs)
            (($# >= 2)) || die "--jobs requires a positive integer"
            JOBS="$2"
            shift 2
            ;;
        --docs-only)
            INSTALL_SCIENCE=false
            INSTALL_DOCS=true
            shift
            ;;
        --science-only)
            INSTALL_SCIENCE=true
            INSTALL_DOCS=false
            shift
            ;;
        --check)
            CHECK_ONLY=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1"
            ;;
    esac
done

if [[ -z "${JOBS}" ]]; then
    if command -v nproc >/dev/null 2>&1; then
        JOBS="$(nproc)"
        if ((JOBS > 8)); then
            JOBS="8"
        fi
    else
        JOBS="4"
    fi
fi
[[ "${JOBS}" =~ ^[1-9][0-9]*$ ]] || die "--jobs must be a positive integer"

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

find_mpi_compilers() {
    local c_candidate=""
    local cxx_candidate=""

    if [[ -n "${MPI_C_COMPILER:-}" || -n "${MPI_CXX_COMPILER:-}" ]]; then
        [[ -n "${MPI_C_COMPILER:-}" && -n "${MPI_CXX_COMPILER:-}" ]] ||
            die "set both MPI_C_COMPILER and MPI_CXX_COMPILER, or neither"
        c_candidate="${MPI_C_COMPILER}"
        cxx_candidate="${MPI_CXX_COMPILER}"
    else
        local pair
        for pair in \
            "mpicc:mpicxx" \
            "mpicc:mpic++" \
            "mpicc.openmpi:mpicxx.openmpi" \
            "mpicc.mpich:mpicxx.mpich"; do
            local c_name="${pair%%:*}"
            local cxx_name="${pair#*:}"
            if command -v "${c_name}" >/dev/null 2>&1 &&
               command -v "${cxx_name}" >/dev/null 2>&1; then
                c_candidate="${c_name}"
                cxx_candidate="${cxx_name}"
                break
            fi
        done
    fi

    [[ -n "${c_candidate}" && -n "${cxx_candidate}" ]] ||
        die "no MPI C/C++ compiler wrapper pair found on PATH"

    MPI_CC="$(command -v "${c_candidate}")"
    MPI_CXX="$(command -v "${cxx_candidate}")"
}

check_prerequisites() {
    if [[ "${INSTALL_SCIENCE}" == true ]]; then
        require_command cmake
        require_command curl
        require_command make
        require_command sha256sum
        require_command tar
        find_mpi_compilers
        printf 'MPI C wrapper:   %s\n' "${MPI_CC}"
        printf 'MPI C++ wrapper: %s\n' "${MPI_CXX}"
    fi

    if [[ "${INSTALL_DOCS}" == true ]]; then
        require_command python3
        python3 -c 'import venv' >/dev/null 2>&1 ||
            die "Python's venv module is unavailable (Ubuntu package: python3-venv)"
        printf 'Python:          %s\n' "$(command -v python3)"
    fi
}

check_prerequisites

if [[ "${CHECK_ONLY}" == true ]]; then
    printf 'Host prerequisites look usable.\n'
    exit 0
fi

mkdir -p "${DEPENDENCY_ROOT}"
DEPENDENCY_ROOT="$(cd -- "${DEPENDENCY_ROOT}" && pwd)"

DOWNLOAD_DIR="${DEPENDENCY_ROOT}/downloads"
SOURCE_DIR="${DEPENDENCY_ROOT}/sources"
BUILD_DIR="${DEPENDENCY_ROOT}/build"
INSTALL_DIR="${DEPENDENCY_ROOT}/install"

mkdir -p "${DOWNLOAD_DIR}" "${SOURCE_DIR}" "${BUILD_DIR}" "${INSTALL_DIR}"

download_archive() {
    local url="$1"
    local checksum="$2"
    local destination="$3"

    if [[ ! -f "${destination}" ]]; then
        local partial="${destination}.part"
        printf 'Downloading %s\n' "${url}"
        curl --fail --location --retry 3 --output "${partial}" "${url}"
        mv -- "${partial}" "${destination}"
    else
        printf 'Reusing %s\n' "${destination}"
    fi

    if ! printf '%s  %s\n' "${checksum}" "${destination}" |
        sha256sum --check --status; then
        die "checksum mismatch for ${destination}; remove it and run again"
    fi
}

extract_archive() {
    local archive="$1"
    local expected_directory="$2"

    if [[ -d "${expected_directory}" ]]; then
        printf 'Reusing source tree %s\n' "${expected_directory}"
        return
    fi

    printf 'Extracting %s\n' "${archive}"
    tar --extract --file "${archive}" --directory "${SOURCE_DIR}"
    [[ -d "${expected_directory}" ]] ||
        die "archive did not create expected directory: ${expected_directory}"
}

install_zlib() {
    local source_path="$1"
    local prefix="${INSTALL_DIR}/zlib"
    local build_path="${BUILD_DIR}/zlib"

    if [[ -f "${prefix}/include/zlib.h" ]]; then
        printf 'zlib is already installed in %s\n' "${prefix}"
        return
    fi

    printf 'Building zlib %s\n' "${ZLIB_VERSION}"
    cmake \
        -S "${source_path}" \
        -B "${build_path}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${prefix}" \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DZLIB_BUILD_EXAMPLES=OFF
    cmake --build "${build_path}" --parallel "${JOBS}"
    cmake --install "${build_path}"
}

install_p4est_variant() {
    local source_path="$1"
    local variant="$2"
    local prefix="${INSTALL_DIR}/p4est/${variant}"
    local build_path="${BUILD_DIR}/p4est/${variant}"
    local zlib_prefix="${INSTALL_DIR}/zlib"
    local cflags="-O3 -DNDEBUG"
    local configure_mode=()

    if [[ "${variant}" == "debug" ]]; then
        cflags="-O0 -g"
        configure_mode+=(--enable-debug)
    fi

    if [[ -f "${prefix}/include/p4est.h" ]]; then
        printf 'p4est (%s) is already installed in %s\n' "${variant}" "${prefix}"
        return
    fi

    printf 'Building p4est %s (%s)\n' "${P4EST_VERSION}" "${variant}"
    mkdir -p "${build_path}"
    (
        cd -- "${build_path}"
        env \
            CC="${MPI_CC}" \
            CXX="${MPI_CXX}" \
            CFLAGS="${cflags}" \
            CPPFLAGS="-DSC_LOG_PRIORITY=SC_LP_ESSENTIAL -I${zlib_prefix}/include" \
            LDFLAGS="-L${zlib_prefix}/lib -Wl,-rpath,${zlib_prefix}/lib" \
            LIBS="-lm" \
            "${source_path}/configure" \
                --prefix="${prefix}" \
                --enable-mpi \
                --enable-shared \
                --disable-vtk-binary \
                --without-blas \
                "${configure_mode[@]}"
    )
    make -C "${build_path}" --jobs="${JOBS}"

    local config_header
    config_header="$(find "${build_path}" -name p4est_config.h -type f -print -quit)"
    [[ -n "${config_header}" ]] || die "p4est configuration header was not generated"
    grep -Eq '^#define P4EST_HAVE_ZLIB +1$' "${config_header}" ||
        die "p4est was not configured with zlib support"
    grep -Eq '^#define P4EST_ENABLE_MPI +1$' "${config_header}" ||
        die "p4est was not configured with MPI support"

    make -C "${build_path}" install
}

install_dealii_variant() {
    local source_path="$1"
    local variant="$2"
    local cmake_build_type="Release"
    local prefix="${INSTALL_DIR}/dealii/${variant}"
    local build_path="${BUILD_DIR}/dealii/${variant}"
    local p4est_prefix="${INSTALL_DIR}/p4est/${variant}"
    local zlib_prefix="${INSTALL_DIR}/zlib"
    local package_config="${prefix}/lib/cmake/deal.II/deal.IIConfig.cmake"

    if [[ "${variant}" == "debug" ]]; then
        cmake_build_type="Debug"
    fi

    if [[ -f "${package_config}" ]]; then
        printf 'deal.II (%s) is already installed in %s\n' "${variant}" "${prefix}"
        return
    fi

    printf 'Building deal.II %s (%s); this can take a while.\n' \
        "${DEALII_VERSION}" "${variant}"

    env \
        LD_LIBRARY_PATH="${p4est_prefix}/lib:${zlib_prefix}/lib:${LD_LIBRARY_PATH:-}" \
        cmake \
            -S "${source_path}" \
            -B "${build_path}" \
            -DCMAKE_BUILD_TYPE="${cmake_build_type}" \
            -DCMAKE_INSTALL_PREFIX="${prefix}" \
            -DCMAKE_PREFIX_PATH="${p4est_prefix};${zlib_prefix}" \
            -DCMAKE_INSTALL_RPATH="${p4est_prefix}/lib;${zlib_prefix}/lib" \
            -DMPI_C_COMPILER="${MPI_CC}" \
            -DMPI_CXX_COMPILER="${MPI_CXX}" \
            -DP4EST_DIR="${p4est_prefix}" \
            -DZLIB_DIR="${zlib_prefix}" \
            -DZLIB_ROOT="${zlib_prefix}" \
            -DDEAL_II_PROJECT_CONFIG_RELDIR="lib/cmake/deal.II" \
            -DDEAL_II_LIBRARY_RELDIR="lib" \
            -DDEAL_II_ALLOW_AUTODETECTION=OFF \
            -DDEAL_II_ALLOW_BUNDLED=ON \
            -DDEAL_II_FORCE_BUNDLED_BOOST=ON \
            -DDEAL_II_WITH_BOOST=ON \
            -DDEAL_II_WITH_KOKKOS=ON \
            -DDEAL_II_WITH_MPI=ON \
            -DDEAL_II_WITH_P4EST=ON \
            -DDEAL_II_WITH_TASKFLOW=ON \
            -DDEAL_II_WITH_TBB=OFF \
            -DDEAL_II_WITH_THREADS=ON \
            -DDEAL_II_WITH_ZLIB=ON \
            -DDEAL_II_COMPONENT_DOCUMENTATION=OFF \
            -DDEAL_II_COMPONENT_EXAMPLES=OFF

    env \
        LD_LIBRARY_PATH="${p4est_prefix}/lib:${zlib_prefix}/lib:${LD_LIBRARY_PATH:-}" \
        cmake --build "${build_path}" --parallel "${JOBS}" --target install

    [[ -f "${package_config}" ]] ||
        die "deal.II installation did not create ${package_config}"
}

install_science_dependencies() {
    local zlib_archive="${DOWNLOAD_DIR}/zlib-${ZLIB_VERSION}.tar.gz"
    local p4est_archive="${DOWNLOAD_DIR}/p4est-${P4EST_VERSION}.tar.gz"
    local dealii_archive="${DOWNLOAD_DIR}/dealii-${DEALII_VERSION}.tar.gz"

    download_archive \
        "https://github.com/madler/zlib/releases/download/v${ZLIB_VERSION}/zlib-${ZLIB_VERSION}.tar.gz" \
        "${ZLIB_SHA256}" \
        "${zlib_archive}"
    download_archive \
        "https://github.com/cburstedde/p4est/releases/download/v${P4EST_VERSION}/p4est-${P4EST_VERSION}.tar.gz" \
        "${P4EST_SHA256}" \
        "${p4est_archive}"
    download_archive \
        "https://github.com/dealii/dealii/releases/download/v${DEALII_VERSION}/dealii-${DEALII_VERSION}.tar.gz" \
        "${DEALII_SHA256}" \
        "${dealii_archive}"

    local zlib_source="${SOURCE_DIR}/zlib-${ZLIB_VERSION}"
    local p4est_source="${SOURCE_DIR}/p4est-${P4EST_VERSION}"
    local dealii_source="${SOURCE_DIR}/dealii-${DEALII_VERSION}"

    extract_archive "${zlib_archive}" "${zlib_source}"
    extract_archive "${p4est_archive}" "${p4est_source}"
    extract_archive "${dealii_archive}" "${dealii_source}"

    install_zlib "${zlib_source}"
    install_p4est_variant "${p4est_source}" debug
    install_p4est_variant "${p4est_source}" release
    install_dealii_variant "${dealii_source}" debug
    install_dealii_variant "${dealii_source}" release
}

install_documentation_dependencies() {
    local virtual_environment="${DEPENDENCY_ROOT}/docs"

    if [[ ! -x "${virtual_environment}/bin/python" ]]; then
        printf 'Creating documentation environment in %s\n' "${virtual_environment}"
        python3 -m venv "${virtual_environment}"
    fi

    "${virtual_environment}/bin/python" -m pip install \
        --disable-pip-version-check \
        --no-cache-dir \
        --requirement "${REPOSITORY_ROOT}/docs/requirements.txt"
}

printf 'Dependency root: %s\n' "${DEPENDENCY_ROOT}"
printf 'Parallel jobs:   %s\n' "${JOBS}"

if [[ "${INSTALL_SCIENCE}" == true ]]; then
    install_science_dependencies
fi

if [[ "${INSTALL_DOCS}" == true ]]; then
    install_documentation_dependencies
fi

printf '\nDependency setup complete.\n'
if [[ "${INSTALL_SCIENCE}" == true ]]; then
    printf 'Configure Rift with: cmake --preset debug\n'
fi
if [[ "${INSTALL_DOCS}" == true ]]; then
    printf 'Build documentation with: cmake --preset docs && cmake --build --preset docs\n'
fi
