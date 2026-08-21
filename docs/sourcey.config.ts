import { defineConfig, markdown } from "sourcey";

export default defineConfig({
  name: "Rift",
  repo: "https://github.com/dannylong-math/rift",
  editBranch: "main",
  editBasePath: "docs",
  navigation: {
    tabs: [
      {
        tab: "Guide",
        slug: "",
        source: markdown({
          groups: [
            {
              group: "Architecture foundations",
              pages: [
                "architecture/00-index",
                "architecture/01-phase-graph",
                "architecture/02-discrete-state",
              ],
            },
            {
              group: "Geometry and routing",
              pages: [
                "architecture/03-geometry-topology",
                "architecture/04-level-set-system",
                "architecture/05-worksets",
                "architecture/06-temporal-geometry-and-gcl",
                "architecture/07-phase-activation",
                "architecture/08-adaptivity-and-transfer",
                "architecture/09-level-set-reinitialization",
              ],
            },
            {
              group: "Operator contracts",
              pages: [
                "architecture/10-phase-systems",
                "architecture/11-model-policies",
                "architecture/12-stabilization-and-admissibility",
                "architecture/13-boundary-operators",
                "architecture/14-interface-operators",
                "architecture/15-regional-constraints",
              ],
            },
            {
              group: "Assembly and solve",
              pages: [
                "architecture/16-operator-evaluation",
                "architecture/17-execution-backend",
                "architecture/18-linear-algebra",
                "architecture/19-preconditioning",
                "architecture/20-nonlinear-coupling",
                "architecture/21-time-integration",
                "architecture/22-solver-lifecycle",
              ],
            },
            {
              group: "Evolution",
              pages: [
                "architecture/23-extension-points",
                "architecture/24-decision-status",
              ],
            },
          ],
        }),
      },
    ],
  },
});
