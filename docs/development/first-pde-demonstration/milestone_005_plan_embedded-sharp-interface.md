# Retained superseded draft: embedded geometry after reactions

| Field | Value |
|---|---|
| Status | Superseded by the user-approved milestone order; retained without deletion |
| Feature | `first-pde-demonstration` |
| Component | Two-phase implicit classification, cut quadrature, and interface worksets |
| Branch | To be recorded when work begins |
| Compatibility | Preserve Milestones 002-004 fitted cases as regression oracles |
| Target environments | 2D then 3D CPU, exactly two phases initially |

The approved sequence now adds embedded moving geometry and phase change in
Milestone 004, before adding reactions in Milestone 005. See
[Milestone 004](milestone_004_plan_moving-phase-changing-interface.md) and
[Milestone 005](milestone_005_plan_bulk-chemical-reactions.md).

## Historical problem and outcome

Replace the demo-only mesh-aligned router with the planned geometry and workset
boundaries so the same bulk and interface equations run on an embedded sharp
interface. This is the first milestone intended to demonstrate Rift's
distinguishing numerical geometry rather than only its state architecture.

```mermaid
flowchart LR
    PSI["Two phase-ranked potentials"] --> CUT["Certified two-phase geometry"]
    CUT --> WS["Bulk + interface worksets"]
    WS --> EQ["Existing verified equations"]
    EQ --> EMBED["Embedded-interface solution"]
```

### Initial boundaries

- Exactly two phases; junction execution remains deferred.
- Static interface before transport, GCL, or topology changes.
- Compare fitted and embedded results on the same planar case before curved
  geometry.

### Exit evidence

- Phase volume and interface-measure verification.
- Cut-quadrature convergence and fitted/embedded equivalence.
- Conservation and PDE convergence on a curved manufactured interface.
