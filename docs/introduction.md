---
title: Introduction
description: Build the clean-slate Rift library and inspect its first public API.
---

# Rift

Rift is a C++23 research library for sharp-interface multiphase flow. It uses
[deal.II 9.8.0](https://github.com/dealii/dealii/releases/tag/v9.8.0), MPI, and
p4est.

The implementation is being rebuilt from a clean foundation. Its current public
surface contains only semantic version information. This deliberately small
starting point allows each scientific component and its contracts to be
reviewed before implementation.

## Build and test

Install the local scientific dependencies, then configure and build from the
repository root:

```console
./scripts/install_dependencies.sh --science-only
cmake --preset debug
cmake --build --preset debug --parallel 6
ctest --preset debug --output-on-failure
```

The debug preset enables AddressSanitizer and UndefinedBehaviorSanitizer.

## Inspect the linked version

```cpp
#include <rift/version.hpp>

#include <iostream>

int main()
{
    const auto version = rift::current_version();
    std::cout << "Rift " << version.major << '.' << version.minor << '.' << version.patch << '\n';
}
```

`current_version()` does not allocate and cannot fail. Later user workflows
will be documented when their APIs have been collaboratively designed and
implemented.

## Build the documentation

```console
cmake -E make_directory build/doxygen
doxygen Doxyfile
npm ci --prefix docs
npm run --prefix docs build
```

The generated site is written to `docs/dist/`.
