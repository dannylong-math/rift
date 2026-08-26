#include <bit>
#include <cstddef>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <iostream>
#include <memory>
#include <mpi.h>
#include <optional>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

/**
 * \brief Let every rank agree whether a tutorial invariant holds.
 * \param[in] local_condition Whether the invariant holds on this rank.
 * \param[in] message Explanation printed by rank zero when any rank fails.
 * \return `true` only when the condition holds on every rank.
 */
bool collective_check(const bool local_condition, const std::string_view message)
{
    const int local_value = local_condition ? 1 : 0;
    const int global_value = dealii::Utilities::MPI::min(local_value, MPI_COMM_WORLD);
    const auto rank = dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD);
    if (global_value == 0 && rank == 0) {
        std::cerr << "Tutorial 3 failed: " << message << '\n';
    }
    return global_value != 0;
}

/**
 * \brief Retain every immutable setup object needed by the state lesson.
 *
 * The declaration order preserves the provenance lifetime: run, graph, and
 * mesh owners outlive the references and finalized space derived from them.
 */
struct TutorialLayout {
    rift::RunConfiguration run;
    rift::PhaseGraph graph;
    std::shared_ptr<const rift::MeshSnapshot<2>> mesh;
    rift::PhaseReference gas;
    rift::PhaseReference liquid;
    rift::SpaceSnapshot<2> space;
};

/**
 * \brief Recreate the cumulative run, graph, mesh, and space setup.
 * \return The complete retained layout, or no value after a collective error.
 */
std::optional<TutorialLayout> build_layout()
{
    // rift:snippet-begin tutorial-003.build-layout
    auto run_result = rift::RunConfiguration::create(MPI_COMM_WORLD);
    if (!collective_check(run_result.has_value(), "Rift could not create the run configuration")) {
        return std::nullopt;
    }
    auto run = std::move(*run_result);

    const rift::InterfaceCompatibilityCheck accept_available_pair =
        [](const rift::PhaseDescriptor&, const rift::PhaseDescriptor&,
           const rift::InterfaceSpecification&) -> std::optional<std::string> { return std::nullopt; };
    auto graph_result =
        rift::make_phase_graph(run, {{"gas", "compressible"}, {"liquid", "low-mach"}},
                               {{"free_surface", "liquid", "gas", "finite-rate"}}, accept_available_pair);
    if (!collective_check(graph_result.has_value(), "the phase graph input was rejected")) {
        return std::nullopt;
    }
    auto graph = std::move(*graph_result);
    const auto gas_id = graph.find_phase("gas");
    const auto liquid_id = graph.find_phase("liquid");
    if (!collective_check(gas_id && liquid_id, "the phase names were not found") || !gas_id || !liquid_id) {
        return std::nullopt;
    }
    const auto gas = graph.reference(*gas_id);
    const auto liquid = graph.reference(*liquid_id);
    if (!collective_check(gas && liquid, "phase references could not be created") || !gas || !liquid) {
        return std::nullopt;
    }

    auto distributed = std::make_unique<dealii::parallel::distributed::Triangulation<2>>(MPI_COMM_WORLD);
    dealii::GridGenerator::subdivided_hyper_cube(*distributed, 2);
    std::unique_ptr<dealii::Triangulation<2>> mesh_input = std::move(distributed);
    auto mesh_result = rift::make_mesh_snapshot(run, std::move(mesh_input));
    if (!collective_check(mesh_result.has_value(), "the distributed mesh could not be retained")) {
        return std::nullopt;
    }
    const auto& mesh = *mesh_result;

    rift::SupportEnvelope gas_cells;
    rift::SupportEnvelope liquid_cells;
    for (const auto& cell : mesh->triangulation().active_cell_iterators()) {
        if (cell->is_locally_owned()) {
            auto& support = cell->center()(0) < 0.5 ? liquid_cells : gas_cells;
            support.insert(cell->id());
        }
    }
    rift::SpaceSpecification specification{
        .phase_fields =
            {
                {.phase = *gas, .name = "gas_state", .components = 4, .polynomial_degree = 1},
                {.phase = *liquid, .name = "liquid_state", .components = 4, .polynomial_degree = 1},
            },
        .level_set = {.name = "level_set", .components = 1, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = *gas, .mesh = mesh->id(), .locally_owned_requested_cells = std::move(gas_cells)},
        {.phase = *liquid, .mesh = mesh->id(), .locally_owned_requested_cells = std::move(liquid_cells)},
    };
    const rift::SpaceRegistry<2> registry(mesh);
    auto draft_result = registry.begin_draft(graph, std::move(specification), std::move(supports));
    if (!collective_check(draft_result.has_value(), "the finite-element space draft was rejected")) {
        return std::nullopt;
    }
    auto space_result = registry.finalize(*draft_result, {{"mean_pressure"}});
    if (!collective_check(space_result.has_value(), "the state layout could not be finalized")) {
        return std::nullopt;
    }
    auto space = std::move(*space_result);
    // rift:snippet-end tutorial-003.build-layout

    return TutorialLayout{.run = std::move(run),
                          .graph = std::move(graph),
                          .mesh = mesh,
                          .gas = *gas,
                          .liquid = *liquid,
                          .space = std::move(space)};
}

/**
 * \brief Fill the entries owned by this MPI rank with one teaching value.
 * \param[in,out] values The owner-partitioned state vector to edit.
 * \param[in] value The value assigned to every locally owned entry.
 */
void set_owned_values(rift::DistributedStateVector& values, const double value)
{
    for (std::size_t index = 0; index < values.locally_owned_size(); ++index) {
        values.local_element(index) = value;
    }
}

/**
 * \brief Check the exact binary representation of every owner-local entry.
 * \param[in] values The immutable distributed vector to inspect.
 * \param[in] expected The binary64 value expected in each locally owned entry.
 * \return `true` only when every owner-local entry has the expected bits.
 */
bool all_owned_values_have_representation(const rift::DistributedStateVector& values, const double expected)
{
    const auto expected_bits = std::bit_cast<std::uint64_t>(expected);
    for (std::size_t index = 0; index < values.locally_owned_size(); ++index) {
        if (std::bit_cast<std::uint64_t>(values.local_element(index)) != expected_bits) {
            return false;
        }
    }
    return true;
}

/** \brief Collect the checked state-field and regional identities used by Tutorial 3. */
struct TutorialStateFields {
    /** \brief Gas state field. */
    std::optional<rift::StateFieldReference> gas;
    /** \brief Liquid state field. */
    std::optional<rift::StateFieldReference> liquid;
    /** \brief Full-background level-set field. */
    std::optional<rift::StateFieldReference> level_set;
    /** \brief Replicated regional scalar. */
    std::optional<rift::RegionalEntryId> regional;
};

/**
 * \brief Check every owner-local field entry and the regional scalar exactly.
 * \param[in] snapshot Immutable state to inspect.
 * \param[in] fields Checked identities that select its values.
 * \param[in] gas Expected gas-field value.
 * \param[in] liquid Expected liquid-field value.
 * \param[in] level_set Expected level-set value.
 * \param[in] regional Expected replicated regional value.
 * \return `true` when all selected values have the requested binary representations.
 */
bool snapshot_has_exact_values(const rift::StateSnapshot& snapshot, const TutorialStateFields& fields, const double gas,
                               const double liquid, const double level_set, const double regional)
{
    if (!fields.gas || !fields.liquid || !fields.level_set || !fields.regional) {
        return false;
    }
    const auto gas_values = snapshot.field(*fields.gas);
    const auto liquid_values = snapshot.field(*fields.liquid);
    const auto level_set_values = snapshot.field(*fields.level_set);
    if (!gas_values || !liquid_values || !level_set_values) {
        return false;
    }
    return all_owned_values_have_representation(gas_values->get(), gas) &&
           all_owned_values_have_representation(liquid_values->get(), liquid) &&
           all_owned_values_have_representation(level_set_values->get(), level_set) &&
           std::bit_cast<std::uint64_t>(snapshot.regional_value(*fields.regional)) ==
               std::bit_cast<std::uint64_t>(regional);
}

/**
 * \brief Create, seal, publish, and inspect one state transaction.
 * \param[in] layout The retained setup objects that define vector meaning.
 * \return Zero after the published and previous snapshots pass all checks.
 */
int publish_update(const TutorialLayout& layout)
{
    const auto& space = layout.space;

    // rift:snippet-begin tutorial-003.create-state
    auto store_result = rift::make_state_store(space.layout());
    if (!collective_check(store_result.has_value(), "the state store could not be created")) {
        return 1;
    }
    auto store = std::move(*store_result);
    const auto initial = store.snapshot(rift::StateSlot::accepted);

    const auto gas_field_id = space.find_field(layout.gas, "gas_state");
    const auto liquid_field_id = space.find_field(layout.liquid, "liquid_state");
    if (!collective_check(gas_field_id && liquid_field_id, "the finalized fields were not found") || !gas_field_id ||
        !liquid_field_id) {
        return 1;
    }
    const auto gas_field = space.layout().field_reference(*gas_field_id);
    const auto liquid_field = space.layout().field_reference(*liquid_field_id);
    const auto level_set_field = space.layout().field_reference(space.level_set_space().id());
    if (!collective_check(gas_field && liquid_field && level_set_field, "field references could not be created") ||
        !gas_field || !liquid_field || !level_set_field) {
        return 1;
    }

    const auto regional_entry = space.layout().regional_entries().front().id;
    const TutorialStateFields fields{
        .gas = gas_field, .liquid = liquid_field, .level_set = level_set_field, .regional = regional_entry};
    if (!collective_check(snapshot_has_exact_values(initial, fields, 0.0, 0.0, 0.0, 0.0),
                          "the initial state is not exact positive zero")) {
        return 1;
    }
    // rift:snippet-end tutorial-003.create-state

    // rift:snippet-begin tutorial-003.edit-trial
    auto trial_result = store.begin_trial_collective(initial.stamp().snapshot);
    if (!collective_check(trial_result.has_value(), "a mutable trial could not be started")) {
        return 1;
    }
    auto trial = std::move(*trial_result);

    auto gas_values_result = trial.field(*fields.gas);
    auto liquid_values_result = trial.field(*fields.liquid);
    auto level_set_values_result = trial.field(*fields.level_set);
    if (!collective_check(gas_values_result && liquid_values_result && level_set_values_result,
                          "the transaction rejected a field reference")) {
        return 1;
    }

    set_owned_values(gas_values_result->get(), 1.0);
    set_owned_values(liquid_values_result->get(), 2.0);
    set_owned_values(level_set_values_result->get(), 0.25);

    const auto regional_result = trial.set_regional_value_collective(regional_entry, 1.25);
    if (!collective_check(regional_result.has_value(), "the regional value update was rejected")) {
        return 1;
    }
    // rift:snippet-end tutorial-003.edit-trial

    // rift:snippet-begin tutorial-003.seal-and-publish
    auto candidate_result = trial.seal_collective();
    if (!collective_check(candidate_result.has_value(), "the mutable trial could not be sealed")) {
        return 1;
    }
    const auto& candidate = *candidate_result;
    const bool candidate_is_private = !candidate.stamp().published_epoch.has_value() &&
                                      candidate.level_set_snapshot() != initial.level_set_snapshot();
    if (!collective_check(candidate_is_private, "the sealed candidate has an unexpected revision stamp")) {
        return 1;
    }

    auto published_result = store.publish_collective(candidate.stamp().snapshot);
    if (!collective_check(published_result.has_value(), "the candidate could not be published")) {
        return 1;
    }
    const auto& accepted = *published_result;
    const auto previous = store.snapshot(rift::StateSlot::previous);
    // rift:snippet-end tutorial-003.seal-and-publish

    // rift:snippet-begin tutorial-003.inspect-snapshots
    const bool identities_are_expected = accepted.stamp().snapshot == candidate.stamp().snapshot &&
                                         accepted.stamp().published_epoch.has_value() &&
                                         accepted.stamp().published_epoch != initial.stamp().published_epoch &&
                                         previous.stamp().snapshot == initial.stamp().snapshot &&
                                         accepted.level_set_snapshot() == candidate.level_set_snapshot() &&
                                         accepted.level_set_snapshot() != initial.level_set_snapshot();
    const bool accepted_values_are_exact = snapshot_has_exact_values(accepted, fields, 1.0, 2.0, 0.25, 1.25);
    const bool previous_is_exact_zero = snapshot_has_exact_values(previous, fields, 0.0, 0.0, 0.0, 0.0);
    if (!collective_check(identities_are_expected && accepted_values_are_exact && previous_is_exact_zero,
                          "accepted and previous snapshots do not preserve the exact update")) {
        return 1;
    }
    // rift:snippet-end tutorial-003.inspect-snapshots

    const auto rank = dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD);
    if (rank == 0) {
        std::cout << "Tutorial 3 complete: one discrete-state update was published.\n";
    }
    return 0;
}

/**
 * \brief Run the cumulative discrete-state lesson while MPI is available.
 * \return Zero after setup and publication succeed on every rank.
 */
int run_tutorial()
{
    auto layout = build_layout();
    if (!layout) {
        return 1;
    }
    return publish_update(*layout);
}

} // namespace

/**
 * \brief Keep the MPI lifetime outside every object created by Tutorial 3.
 * \param[in] argc Number of command-line arguments.
 * \param[in] argv Command-line arguments forwarded to the MPI runtime.
 * \return The collective tutorial result.
 */
int main(int argc, char** argv)
{
    const dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    return run_tutorial();
}
