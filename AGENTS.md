# Rift contribution guidance

## Unit tests

- Put C++ unit tests in `tests/`; CMake discovers every `.cpp` file there.
- Keep each test file small, self-contained, and focused on one behavior.
- Prefer several readable files over one large fixture-heavy suite.
- Name files `<subject>_<behavior>_<two-digit-sequence>.cpp`, for example
  `foo_bar_00.cpp`, `foo_bar_01.cpp`, and `phase_graph_lookup_00.cpp`.
- Use a shared header only when it removes incidental setup without hiding the
  behavior under test.
- Calculate line coverage after every implementation. Project code in
  `include/` and `src/` must have 100% line coverage before the work is
  complete.
- When coverage is below 100%, inspect the report for the exact uncovered
  lines and add small, focused unit tests that exercise those behaviors. Run
  the complete test suite and coverage report again until both pass and line
  coverage reaches 100%.

The generated contributor guide in `docs/contributing.md` is the canonical
human-facing version of these instructions.

## C++ documentation

- Document every C++ entity, including private members, file-local helpers,
  and types in `rift::detail`.
- Write Doxygen comments as `/** ... */` blocks. Do not use `///` comments.
- Document template parameters, function parameters, return values, and thrown
  exceptions where applicable.
- Write `\brief` from the reader's perspective: state what the entity helps the
  reader accomplish rather than merely restating its declaration.
- Use a structured guided-reference style in extended descriptions. Add
  `\par When to use`, `\par Typical use`, `\par Important behavior`, and
  `\par Failure handling` when those sections are relevant.
- Give every class and struct a realistic, complete `\code{.cpp}` example in
  its extended description. Show the full intended workflow rather than an
  isolated expression or pseudocode, and use only APIs that actually exist.
- For standalone functions and important entry points, include a complete
  example. For small members, explain their place in the class workflow and
  use a focused snippet instead of copying the full class example.
- Write public documentation for library users. Write private, file-local, and
  `rift::detail` documentation for maintainers, explaining the internal usage
  pattern and the invariant the entity preserves.
- Prefer explanations of ownership, lifetime, error handling, and important
  invariants over descriptions of implementation syntax visible in the
  declaration itself.
- Keep Doxygen configured to extract private, static, anonymous-namespace, and
  detail-namespace implementation documentation and to fail on missing docs.
