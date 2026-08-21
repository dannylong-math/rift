---
title: Introduction
description: Build Rift and explore its architecture and generated C++ API reference.
---

# Rift

Rift is a C++23 research code for sharp-interface multiphase flow. It uses
[deal.II 9.8.0](https://github.com/dealii/dealii/releases/tag/v9.8.0), MPI, and
p4est.

The project is at an early stage. Its documentation records the contracts and
design decisions that will guide the implementation, alongside a C++ API
reference generated from the public headers.

## Build and test

Install the local scientific dependencies, then configure and build Rift from
the repository root:

```shell
./scripts/install_dependencies.sh --science-only
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
```

The debug preset enables AddressSanitizer and UndefinedBehaviorSanitizer.
Release, `debug-tidy`, and `release-max` presets are also available.

## Build the documentation

Install Doxygen, Node.js 22.12 or newer, and the pinned Sourcey dependencies.
Then generate Doxygen XML and build the static site:

```shell
cmake -E make_directory build/doxygen
doxygen Doxyfile
npm ci --prefix docs
npm run --prefix docs build
```

The generated site is written to `docs/dist/`.
