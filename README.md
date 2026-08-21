# Rift

Rift is a C++23 research code for sharp-interface multiphase flow. It uses
[deal.II 9.8.0](https://github.com/dealii/dealii/releases/tag/v9.8.0), MPI, and
p4est.

## Prerequisites

The setup is intended for Ubuntu and performs no system-wide installation. The
host must already provide:

- a C++23 compiler, CMake 3.21 or newer, GNU Make, curl, tar, and `sha256sum`;
- an MPI installation with C and C++ compiler wrappers; and
- Doxygen and Node.js 22.12 or newer for documentation.

On Ubuntu these are normally supplied by packages such as `build-essential`,
`cmake`, `curl`, `doxygen`, and either `libopenmpi-dev` or `libmpich-dev`.
Install Node.js 24 through a version manager rather than Ubuntu's potentially
older `nodejs` package. The Rift installer never invokes `apt` or `sudo`.

## Local dependencies

Run:

```console
./scripts/install_dependencies.sh
```

The script detects an installed MPI implementation and builds the following
under `.dependencies/`:

- zlib 1.3.1;
- p4est 2.8.7 in Debug and Release configurations;
- deal.II 9.8.0 in Debug and Release configurations; and
- the pinned Sourcey packages used to build the documentation.

Downloads are version-pinned and checksum-verified. Existing successful
installations are reused. The full deal.II build requires several gigabytes of
disk space and can take a substantial amount of time. Useful alternatives are:

```console
./scripts/install_dependencies.sh --check
./scripts/install_dependencies.sh --docs-only
./scripts/install_dependencies.sh --science-only --jobs 12
./scripts/install_dependencies.sh --science-only --variant debug
./scripts/install_dependencies.sh --prefix /path/to/rift-dependencies
```

The CMake presets use `.dependencies` by default. If `--prefix` is used, pass
the same root when configuring, for example:

```console
cmake --preset debug -DRIFT_DEPENDENCIES_DIR=/path/to/rift-dependencies
```

## Build and test

CMake automatically selects the matching deal.II installation: Debug presets
use the Debug library, while Release and `release-max` use the Release library.

```console
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug
```

Replace `debug` with `release`, `debug-tidy`, or `release-max` as needed.

## Documentation

Generate Doxygen XML and build the Sourcey site with:

```console
cmake -E make_directory build/doxygen
doxygen Doxyfile
npm ci --prefix docs
npm run --prefix docs build
```

Open `docs/dist/index.html`. Sourcey builds the narrative guides from Markdown
and the C++ API reference from the Doxygen XML generated from public headers.
GitHub Actions builds the site on pull requests and deploys it to GitHub Pages
after pushes to `main`.
