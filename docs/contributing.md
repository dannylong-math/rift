---
title: Contributing
description: Focused test layout, verification commands, and documentation expectations for Rift changes.
---

# Contributing

Rift favors changes whose contracts can be understood from a small number of
nearby files. An implementation change should normally include focused tests,
public API comments, and an update to the relevant architecture page.

## Unit-test layout

CMake automatically discovers every `.cpp` file under `tests/` and builds it
as a separate test executable. Keep each file small, self-contained, and
focused on one behavior. A reader should be able to identify the exercised
contract without first understanding a large fixture or unrelated setup.

Prefer several short test files over one large suite. Name them using

```text
<subject>_<behavior>_<two-digit-sequence>.cpp
```

For example, tests for `Foo::bar` should be named `foo_bar_00.cpp`,
`foo_bar_01.cpp`, and so on. The phase-graph tests follow the same convention:
`phase_graph_construct_00.cpp`, `phase_graph_lookup_00.cpp`, and
`phase_graph_reject_self_edge_00.cpp`.

A small shared header is appropriate when it removes incidental boilerplate,
but it must not hide the inputs, operation, or expectation that define the
test. Test-only helpers use `.hpp` so CMake does not create an executable for
them.

After implementing a change, run the complete test suite and calculate line
coverage for project code in `include/` and `src/`. Line coverage must be 100%
before the implementation is complete. If the report is below 100%, identify
the exact uncovered lines, add small focused tests for those behaviors, and
repeat the tests and coverage report until both pass at 100%.

## Build and test

Configure, compile, and run the default debug suite with:

```console
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug
```

Also run the release suite when a change affects public interfaces, numerical
code, ownership, or optimization-sensitive behavior. Run `./format.sh` before
submitting a change.

## Documentation

Document every C++ entity, not only the public interface. This includes
private constructors and data members, file-local helpers, and types in the
`rift::detail` namespace. Doxygen extracts these implementation details, and
the documentation build fails when an entity is undocumented.

Use block-form Doxygen comments exclusively:

```cpp
/**
 * Return the phase with the requested stable identity.
 *
 * \param id phase identity to resolve.
 * \return immutable descriptor associated with `id`.
 * \throws std::out_of_range if `id` is not present.
 */
```

Do not use `///` comments. Document template parameters, function parameters,
return values, and thrown exceptions where applicable.

### Explanatory API style

Write API documentation as a structured guide to using the entity. The
`\brief` sentence states what the reader can accomplish. Use the following
extended-description sections when they apply:

- `\par When to use` places the entity in the Rift workflow;
- `\par Typical use` contains a realistic `\code{.cpp}` example;
- `\par Important behavior` explains ownership, lifetime, invariants, and
  performance-relevant behavior; and
- `\par Failure handling` explains errors, exceptions, and invalid inputs.

Every class and struct must contain a complete usage example. The example
should demonstrate its intended workflow using APIs that actually exist; do
not substitute pseudocode or unexplained placeholders. Standalone functions
and important entry points also receive complete examples. Small member
functions use focused snippets and refer back to the class workflow rather
than duplicating the entire example.

Public documentation addresses library users. Documentation for private
members, file-local helpers, and `rift::detail` types addresses maintainers and
explains both the supported internal usage pattern and the invariant it
preserves.

Architectural contracts live in the numbered pages under
`docs/architecture/`. When code or architecture documentation changes,
regenerate the API XML and Sourcey site:

```console
cmake -E make_directory build/doxygen
doxygen Doxyfile
npm run --prefix docs build
```

The rebuilt guide is written to `docs/dist/`.
