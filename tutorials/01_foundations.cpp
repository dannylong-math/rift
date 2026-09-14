/**
 * \file
 * \brief Tutorial 01: construct and publish Rift's complete foundation state.
 *
 * This tutorial is intentionally comment-heavy. It shows the application-side
 * order in which the current foundation objects are meant to be used; it is
 * not yet a flow solver and does not claim to classify physical phase regions.
 */

#include <cstddef>
#include <deal.II/base/point.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/fe/mapping_q1.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/cell_id.h>
#include <memory>
#include <rift/field_group_space.hpp>
#include <rift/logging.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/phase_support.hpp>
#include <rift/rift_context.hpp>
#include <rift/space_draft.hpp>
#include <rift/space_snapshot.hpp>
#include <rift/state_store.hpp>
#include <utility>
#include <vector>

int main(int argc, char** argv) // NOLINT(readability-function-cognitive-complexity): linear narrative tutorial.
{
    // RiftContext is the first Rift object in an application. It initializes
    // deal.II, p4est, and MPI, and it must outlive every object created below.
    // By default its logger prints informational messages only on world rank 0.
    rift::RiftContext context(argc, argv);

    // The phase graph is configuration, not geometry. It says that this run
    // knows about air and water and permits one oriented material interface.
    // It does not decide which cells contain either phase.
    auto graph_result = context.create_phase_graph(
        {.phases = {{.name = "water", .physics_key = rift::PhysicsKey{"incompressible"}},
                    {.name = "air", .physics_key = rift::PhysicsKey{"compressible"}}},
         .interfaces = {{.name = "free-surface",
                         .minus_phase = "water",
                         .plus_phase = "air",
                         .operator_key = rift::InterfaceOperatorKey{"material-interface"}}}},
        rift::interface_compatibility::accept_all);
    if (!graph_result) {
        // Scientific factories never print automatically. The application
        // explicitly chooses how to present their structured diagnostics.
        context.logger().error("Phase-graph construction failed:\n{}",
                               rift::format_phase_graph_errors(graph_result.error()));
        return 1;
    }
    const auto& graph = graph_result->get();
    const auto air_id = graph.find_phase("air");
    const auto water_id = graph.find_phase("water");
    if (!air_id || !water_id) {
        context.logger().critical("The accepted graph is missing a configured phase");
        return 1;
    }
    const auto air = air_id.value();
    const auto water = water_id.value();
    context.logger().info("Created a graph with {} phases and {} interface", graph.phases().size(),
                          graph.interfaces().size());

    // MeshSnapshot takes ownership of a completed distributed triangulation
    // and its mapping. Code outside the owner receives only immutable access.
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<2>>(
        context.mpi_communicator());
    dealii::GridGenerator::subdivided_hyper_rectangle(
        *triangulation, {8U, 4U}, dealii::Point<2>{0.0, 0.0}, dealii::Point<2>{2.0, 1.0});
    auto mesh_result =
        context.create_mesh_snapshot<2>(std::move(triangulation), std::make_unique<dealii::MappingQ1<2>>());
    if (!mesh_result) {
        context.logger().error("Mesh publication failed with {} structured issue(s)", mesh_result.error().size());
        return 1;
    }
    const auto& mesh = *mesh_result;

    // A phase support is an algebraic safety envelope. Here we request water
    // cells to the left of x=1.15 and air cells to the right of x=0.85. The
    // overlap intentionally leaves room in which a future moving interface
    // could be represented. These requests do not assert physical occupancy.
    std::vector<dealii::CellId> water_cells;
    std::vector<dealii::CellId> air_cells;
    for (const auto& cell : mesh->triangulation().active_cell_iterators()) {
        if (!cell->is_locally_owned()) {
            continue;
        }
        if (cell->center()(0) < 1.15) {
            water_cells.push_back(cell->id());
        }
        if (cell->center()(0) > 0.85) {
            air_cells.push_back(cell->id());
        }
    }
    auto supports_result = context.create_phase_supports<2>(
        mesh, {{.phase = water, .requested_cells = std::move(water_cells)},
               {.phase = air, .requested_cells = std::move(air_cells)}});
    if (!supports_result) {
        context.logger().error("Phase-support closure failed with {} structured issue(s)",
                               supports_result.error().size());
        return 1;
    }

    // The space schema describes meanings before allocating deal.II objects.
    // Each phase gets one support-restricted field group. The geometry group
    // called "phase-potentials" demonstrates the first concrete multiphase
    // configuration: one component per canonical phase, interpreted later by
    // a ranking algorithm. The separate unbound carrier demonstrates compact
    // component-aligned phase-label metadata without choosing it as the sole
    // occupancy representation.
    rift::SpaceSpecification specification{
        .phase_support_fields = {{.phase = water, .name = "velocity", .component_count = 2, .degree = 1},
                                 {.phase = air, .name = "pressure", .component_count = 1, .degree = 1}},
        .geometry = {
            .continuous_fields =
                {{.name = "phase-potentials",
                  .components = rift::PhaseBoundFieldComponents{.phases = {water, air}},
                  .degree = 1},
                 {.name = "phase-label-carrier",
                  .components = rift::UnboundFieldComponents{.count = 1},
                  .degree = 1}},
            .discrete_metadata = {{.name = "phase-label",
                                   .geometry_field_group = "phase-label-carrier",
                                   .component = 0,
                                   .kind = rift::DiscreteGeometryMetadataKind::phase_label}}}};

    auto draft_result = context.create_space_draft<2>(std::move(*supports_result), std::move(specification));
    if (!draft_result) {
        context.logger().error("Space-schema validation failed with {} structured issue(s)",
                               draft_result.error().size());
        return 1;
    }
    auto draft = std::move(*draft_result);

    // Building creates one FESystem and one independently numbered DoFHandler
    // per field group. Component-wise numbering is optional; native deal.II
    // numbering is the default. Selecting it here demonstrates the explicit
    // application choice without changing the semantic field layout.
    if (auto built = context.build_field_spaces(draft, {.numbering = rift::DofNumbering::component_wise}); !built) {
        context.logger().error("Field-space construction failed with {} structured issue(s)", built.error().size());
        return 1;
    }

    // Regional entries are nonspatial scalars. A low-Mach model might need one
    // thermodynamic pressure per connected region, while another model can
    // simply finalize an empty regional schema. Rift assigns deterministic
    // round-robin MPI owners and stores readable replicas on every rank.
    auto space_result =
        context.finalize_space(draft, {{.phase = water, .name = "thermodynamic-pressure"}});
    if (!space_result) {
        context.logger().error("Space finalization failed with {} structured issue(s)", space_result.error().size());
        return 1;
    }
    const auto& space = *space_result;
    context.logger().info("Finalized space epoch {} with semantic cardinality {}", space->epoch().value(),
                          space->state_layout().total_cardinality());

    // StateStore is the unique mutable publication authority. RiftContext
    // creates it but deliberately does not retain it. The accepted root is
    // already immutable and contains numeric zeros plus unassigned labels.
    auto store_result = context.create_state_store<2>(space, {.max_pinned_snapshots = 1});
    if (!store_result) {
        context.logger().error("State allocation failed with {} structured issue(s)", store_result.error().size());
        return 1;
    }
    auto store = std::move(*store_result);
    const auto root = store->accepted();
    context.logger().info("Allocated state store {} with root snapshot {}", store->id().value(),
                          root->stamp().snapshot_id.value());

    // Applications discover typed IDs by scientific names, then ask the
    // finalized space for references carrying the SpaceEpoch. Passing a
    // reference from another space is therefore detected instead of silently
    // interpreting the same small integer under the wrong layout.
    const auto velocity_id = space->find_phase_support_field(water, "velocity");
    const auto potentials_id = space->find_geometry_field("phase-potentials");
    const auto labels_id = space->find_discrete_geometry_metadata("phase-label");
    const auto pressure_id = space->find_regional_entry(water, "thermodynamic-pressure");
    if (!velocity_id || !potentials_id || !labels_id || !pressure_id) {
        context.logger().critical("The finalized space is missing a configured state block");
        return 1;
    }
    const auto velocity_reference = space->phase_support_field_reference(velocity_id.value());
    const auto potentials_reference = space->geometry_field_reference(potentials_id.value());
    const auto labels_reference = space->discrete_geometry_metadata_reference(labels_id.value());
    const auto pressure_reference = space->regional_entry_reference(pressure_id.value());

    // Mutable work happens only inside a transaction cloned from the current
    // accepted root. The examples below merely make recognizable values; a
    // future solver will compute them from physical initial conditions.
    auto transaction_result = store->begin_transaction();
    if (!transaction_result) {
        context.logger().error("Could not begin a state transaction: {} issue(s)", transaction_result.error().size());
        return 1;
    }
    auto transaction = std::move(*transaction_result);

    auto& velocity = transaction.phase_support_field(velocity_reference);
    for (std::size_t local = 0; local < velocity.locally_owned_size(); ++local) {
        velocity.local_element(local) = 0.01 * static_cast<double>(local + 1U);
    }
    auto& potentials = transaction.geometry_field(potentials_reference);
    for (std::size_t local = 0; local < potentials.locally_owned_size(); ++local) {
        potentials.local_element(local) = static_cast<double>(context.this_mpi_process() + 1U);
    }
    auto labels = transaction.discrete_geometry_metadata(labels_reference);
    for (const auto dof : labels.locally_owned_dofs()) {
        labels.set_label(dof, dof % 2U == 0U ? water : air);
    }

    // Only the deterministic owner may stage a regional update. There is no
    // communication here; all staged regional values are exchanged once as
    // part of the collective seal below.
    const auto regional_id = pressure_reference.regional_entry;
    const auto& regional_entry = space->state_layout().regional_entry(regional_id);
    if (context.this_mpi_process() == regional_entry.owner_rank) {
        if (auto updated = transaction.set_regional(pressure_reference, 101325.0); !updated) {
            context.logger().error("The regional owner rejected its update: {}", updated.error().message);
            return 1;
        }
    }

    // Seal synchronizes ghost entries, replicates exact regional bits, assigns
    // an immutable snapshot ID, and advances GeometryRevision because the
    // primitive potential values changed. It still does not accept the trial.
    auto candidate_result = transaction.seal();
    if (!candidate_result) {
        context.logger().error("State sealing failed with {} structured issue(s)", candidate_result.error().size());
        return 1;
    }
    const auto& candidate = *candidate_result;

    // Publication is a separate collective decision. This separation lets a
    // future nonlinear or time-step controller audit a candidate before
    // replacing the current accepted state.
    auto publication = store->publish(candidate->reference());
    if (!publication) {
        context.logger().error("State publication failed with {} structured issue(s)", publication.error().size());
        return 1;
    }

    const auto base_snapshot_id = candidate->stamp().base_snapshot_id;
    if (!base_snapshot_id) {
        context.logger().critical("A sealed candidate unexpectedly has no base snapshot");
        return 1;
    }
    context.logger().info("Accepted snapshot {} from root {} at geometry revision {}",
                          candidate->stamp().snapshot_id.value(), base_snapshot_id.value().value(),
                          candidate->stamp().geometry_revision.value());
    context.logger().info("Replicated thermodynamic pressure is {} Pa", candidate->regional(pressure_reference));

    // ConditionalOStream is provided for deal.II APIs and familiar stream-style
    // progress output. It is active only on rank zero and is intentionally not
    // routed to the per-rank spdlog files.
    context.pcout() << "Tutorial 01 completed on " << context.n_mpi_processes() << " MPI rank(s).\n";
    context.logger().flush();
    return 0;
}
