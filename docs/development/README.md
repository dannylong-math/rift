# Development records

The `foundations/` and `first-pde-demonstration/` directories are archived plans
from before the library reset. Their task states and architecture describe
historical work and are not active requirements for the redesigned library.

The corresponding implementation is preserved at Git tag
`reference/pre-reset-status`. Inspect a historical file without switching
branches:

```console
git show reference/pre-reset-status:tutorials/01_foundations.cpp
git ls-tree -r --name-only reference/pre-reset-status
```

Current reset decisions and evidence live under `library-reset/`.
