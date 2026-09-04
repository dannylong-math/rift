#include <algorithm>
#include <boost/ut.hpp>
#include <cstddef>
#include <deal.II/base/mpi.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/fe/mapping_q1.h>
#include <deal.II/grid/cell_id.h>
#include <deal.II/grid/grid_generator.h>
#include <map>
#include <memory>
#include <mpi.h>
#include <optional>
#include <rift/field_group_space.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/phase_support.hpp>
#include <rift/rift_context.hpp>
#include <rift/space_draft.hpp>
#include <string>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] constexpr rift::PhaseId air_phase() noexcept { return rift::PhaseId::from_index(0); }

[[nodiscard]] constexpr rift::PhaseId water_phase() noexcept { return rift::PhaseId::from_index(1); }

[[nodiscard]] rift::PhaseGraphSpecification phase_graph_specification()
{
    return {.phases = {{.name = "water", .physics_key = rift::PhysicsKey{"incompressible"}},
                       {.name = "air", .physics_key = rift::PhysicsKey{"compressible"}}},
            .interfaces = {}};
}

[[nodiscard]] rift::SpaceSpecification space_specification()
{
    return {.phase_support_fields = {{.phase = air_phase(), .name = "pressure", .component_count = 1, .degree = 1}},
            .geometry = {.continuous_fields = {{.name = "phase-potentials",
                                                .components = rift::PhaseBoundFieldComponents{.phases = {water_phase(),
                                                                                                         air_phase()}},
                                                .degree = 1}},
                         .discrete_metadata = {}}};
}

template<int dim>
[[nodiscard]] std::shared_ptr<const rift::MeshSnapshot<dim>> make_snapshot(rift::RiftContext& context,
                                                                           const unsigned int refinements = 2)
{
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    dealii::GridGenerator::hyper_cube(*triangulation);
    triangulation->refine_global(refinements);
    auto result =
        context.create_mesh_snapshot<dim>(std::move(triangulation), std::make_unique<dealii::MappingQ1<dim>>());
    boost::ut::expect(result.has_value());
    return result.has_value() ? *result : nullptr;
}

template<int dim>
[[nodiscard]] rift::PhaseSupportResult<dim>
make_supports(rift::RiftContext& context, const std::shared_ptr<const rift::MeshSnapshot<dim>>& snapshot)
{
    std::vector<dealii::CellId> locally_owned_air_cells;
    for (const auto& cell : snapshot->triangulation().active_cell_iterators()) {
        if (cell->is_locally_owned() &&
            cell->center()[0] < 0.5) { // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
            locally_owned_air_cells.push_back(cell->id());
        }
    }
    return context.create_phase_supports<dim>(
        snapshot, {{.phase = water_phase(), .requested_cells = {}},
                   {.phase = air_phase(), .requested_cells = std::move(locally_owned_air_cells)}});
}

template<int dim>
[[nodiscard]] std::optional<rift::SpaceDraft<dim>>
make_draft(rift::RiftContext& context, const std::shared_ptr<const rift::MeshSnapshot<dim>>& snapshot)
{
    auto supports = make_supports(context, snapshot);
    boost::ut::expect(supports.has_value());
    if (!supports.has_value()) {
        return std::nullopt;
    }
    auto draft = context.create_space_draft<dim>(std::move(*supports), space_specification());
    boost::ut::expect(draft.has_value());
    return draft.has_value() ? std::optional<rift::SpaceDraft<dim>>{std::move(*draft)} : std::nullopt;
}

[[nodiscard]] std::size_t count_code(const rift::FieldSpaceBuildErrors& errors,
                                     const rift::FieldSpaceBuildErrorCode code)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(errors, [code](const auto& error) { return error.code == code; }));
}

void test_distributed_construction(rift::RiftContext& context)
{
    const auto snapshot = make_snapshot<2>(context);
    if (snapshot == nullptr) {
        return;
    }
    auto draft = make_draft(context, snapshot);
    boost::ut::expect(draft.has_value());
    if (!draft.has_value()) {
        return;
    }

    const auto result = context.build_field_spaces(*draft);
    boost::ut::expect(result.has_value());
    if (!result.has_value()) {
        return;
    }

    const auto& phase_space = draft->phase_support_field_spaces().front();
    const auto& geometry_space = draft->geometry_field_spaces().front();
    boost::ut::expect(phase_space.dof_handler().n_dofs() == 15U);
    boost::ut::expect(geometry_space.dof_handler().n_dofs() == 50U);
    boost::ut::expect(phase_space.locally_owned_dofs().is_subset_of(phase_space.locally_relevant_dofs()));
    boost::ut::expect(geometry_space.locally_owned_dofs().is_subset_of(geometry_space.locally_relevant_dofs()));

    using ActiveIndexRecord = std::pair<std::string, unsigned int>;
    std::vector<ActiveIndexRecord> local_owner_records;
    bool inspected_ghost = false;
    for (const auto& cell : phase_space.dof_handler().active_cell_iterators()) {
        if (cell->is_locally_owned()) {
            local_owner_records.emplace_back(cell->id().to_string(), cell->active_fe_index());
        }
    }
    const auto gathered_owner_records =
        dealii::Utilities::MPI::all_gather(context.mpi_communicator(), local_owner_records);
    std::map<std::string, unsigned int> owner_indices;
    for (const auto& rank_records : gathered_owner_records) {
        for (const auto& [cell, index] : rank_records) {
            const auto [position, inserted] = owner_indices.emplace(cell, index);
            boost::ut::expect(inserted || position->second == index);
        }
    }

    for (const auto& cell : phase_space.dof_handler().active_cell_iterators()) {
        if (cell->is_artificial()) {
            continue;
        }
        const auto owner = owner_indices.find(cell->id().to_string());
        boost::ut::expect(owner != owner_indices.end());
        if (owner != owner_indices.end()) {
            boost::ut::expect(cell->active_fe_index() == owner->second);
        }
        const auto expected =
            cell->center()[0] < 0.5 // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
                ? rift::PhaseSupportFieldGroupSpace<2>::ordinary_fe_index
                : rift::PhaseSupportFieldGroupSpace<2>::outside_support_fe_index;
        boost::ut::expect(cell->active_fe_index() == expected);
        inspected_ghost = inspected_ghost || cell->is_ghost();
    }
    if (context.n_mpi_processes() > 1) {
        boost::ut::expect(inspected_ghost);
    }

    const auto rebuild = context.build_field_spaces(*draft);
    boost::ut::expect(not rebuild.has_value());
    if (!rebuild.has_value()) {
        boost::ut::expect(count_code(rebuild.error(), rift::FieldSpaceBuildErrorCode::field_spaces_already_built) ==
                          context.n_mpi_processes());
    }
}

void test_distributed_construction_3d(rift::RiftContext& context)
{
    const auto snapshot = make_snapshot<3>(context, 1);
    if (snapshot == nullptr) {
        return;
    }
    auto draft = make_draft(context, snapshot);
    boost::ut::expect(draft.has_value());
    if (!draft.has_value()) {
        return;
    }

    const auto result = context.build_field_spaces(*draft);
    boost::ut::expect(result.has_value());
    if (!result.has_value()) {
        return;
    }

    const auto& phase_space = draft->phase_support_field_spaces().front();
    boost::ut::expect(phase_space.dof_handler().n_dofs() == 18U);
    boost::ut::expect(draft->geometry_field_spaces().front().dof_handler().n_dofs() == 54U);
    for (const auto& cell : phase_space.dof_handler().active_cell_iterators()) {
        if (cell->is_artificial()) {
            continue;
        }
        const auto expected =
            cell->center()[0] < 0.5 // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
                ? rift::PhaseSupportFieldGroupSpace<3>::ordinary_fe_index
                : rift::PhaseSupportFieldGroupSpace<3>::outside_support_fe_index;
        boost::ut::expect(cell->active_fe_index() == expected);
    }
}

void test_numbering_mismatch(rift::RiftContext& context)
{
    const auto snapshot = make_snapshot<2>(context);
    if (snapshot == nullptr) {
        return;
    }
    auto draft = make_draft(context, snapshot);
    boost::ut::expect(draft.has_value());
    if (!draft.has_value()) {
        return;
    }

    auto numbering = rift::DofNumbering::native;
    if (context.this_mpi_process() == 1) {
        numbering = rift::DofNumbering::component_wise;
    }
    if (context.this_mpi_process() == 2) {
        // Deliberately exercise diagnostic formatting for an unknown wire value.
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        numbering = static_cast<rift::DofNumbering>(99);
    }
    const auto result = context.build_field_spaces(*draft, {.numbering = numbering});
    if (context.n_mpi_processes() == 1) {
        boost::ut::expect(result.has_value());
        return;
    }
    boost::ut::expect(not result.has_value());
    if (!result.has_value()) {
        boost::ut::expect(
            count_code(result.error(), rift::FieldSpaceBuildErrorCode::collective_dof_numbering_mismatch) ==
            context.n_mpi_processes() - 1U);
        boost::ut::expect(std::ranges::any_of(
            result.error(), [](const auto& error) { return error.message.contains("component_wise"); }));
        if (context.n_mpi_processes() == 3) {
            boost::ut::expect(std::ranges::any_of(
                result.error(), [](const auto& error) { return error.message.contains("unknown (99)"); }));
        }
        boost::ut::expect(not draft->field_spaces_built());
    }
}

void test_epoch_mismatch(rift::RiftContext& context)
{
    const auto snapshot = make_snapshot<2>(context);
    if (snapshot == nullptr) {
        return;
    }
    auto first = make_draft(context, snapshot);
    auto second = make_draft(context, snapshot);
    boost::ut::expect(first.has_value() && second.has_value());
    if (!first.has_value() || !second.has_value()) {
        return;
    }

    auto& selected = context.this_mpi_process() == 0 ? *first : *second;
    const auto result = context.build_field_spaces(selected);
    if (context.n_mpi_processes() == 1) {
        boost::ut::expect(result.has_value());
        return;
    }
    boost::ut::expect(not result.has_value());
    if (!result.has_value()) {
        boost::ut::expect(count_code(result.error(), rift::FieldSpaceBuildErrorCode::collective_space_epoch_mismatch) ==
                          context.n_mpi_processes() - 1U);
        boost::ut::expect(not selected.field_spaces_built());
    }
}

void test_inactive_state_mismatch(rift::RiftContext& context)
{
    const auto snapshot = make_snapshot<2>(context);
    if (snapshot == nullptr) {
        return;
    }
    auto draft = make_draft(context, snapshot);
    boost::ut::expect(draft.has_value());
    if (!draft.has_value()) {
        return;
    }

    std::optional<rift::SpaceDraft<2>> moved_owner;
    if (context.n_mpi_processes() == 1 || context.this_mpi_process() != 0) {
        moved_owner.emplace(std::move(*draft));
    }
    // Deliberately validate the documented moved-from state.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    const auto result = context.build_field_spaces(*draft);
    boost::ut::expect(not result.has_value());
    if (!result.has_value()) {
        const auto inactive_count = context.n_mpi_processes() == 1 ? 1U : context.n_mpi_processes() - 1U;
        boost::ut::expect(count_code(result.error(), rift::FieldSpaceBuildErrorCode::inactive_draft) == inactive_count);
        const auto mismatch_count = context.n_mpi_processes() == 1 ? 0U : context.n_mpi_processes() - 1U;
        boost::ut::expect(count_code(result.error(), rift::FieldSpaceBuildErrorCode::collective_draft_state_mismatch) ==
                          mismatch_count);
    }
}

template<int dim> void test_built_state_mismatch(rift::RiftContext& context, const unsigned int refinements)
{
    const auto snapshot = make_snapshot<dim>(context, refinements);
    if (snapshot == nullptr) {
        return;
    }
    auto draft = make_draft(context, snapshot);
    boost::ut::expect(draft.has_value());
    if (!draft.has_value()) {
        return;
    }
    boost::ut::expect(context.build_field_spaces(*draft).has_value());

    auto unbuilt_draft = make_draft(context, snapshot);
    boost::ut::expect(unbuilt_draft.has_value());
    if (!unbuilt_draft.has_value()) {
        return;
    }

    auto& selected = context.n_mpi_processes() == 1 || context.this_mpi_process() != 0 ? *draft : *unbuilt_draft;
    const auto result = context.build_field_spaces(selected);
    boost::ut::expect(not result.has_value());
    if (!result.has_value()) {
        const auto built_count = context.n_mpi_processes() == 1 ? 1U : context.n_mpi_processes() - 1U;
        boost::ut::expect(count_code(result.error(), rift::FieldSpaceBuildErrorCode::field_spaces_already_built) ==
                          built_count);
        const auto mismatch_count = context.n_mpi_processes() == 1 ? 0U : context.n_mpi_processes() - 1U;
        boost::ut::expect(count_code(result.error(), rift::FieldSpaceBuildErrorCode::collective_draft_state_mismatch) ==
                          mismatch_count);
        boost::ut::expect(count_code(result.error(), rift::FieldSpaceBuildErrorCode::collective_space_epoch_mismatch) ==
                          mismatch_count);
    }
}

} // namespace

int main(int argc, char** argv) // NOLINT(readability-function-cognitive-complexity): Boost.UT registration.
{
    using namespace boost::ut;

    rift::RiftContext actual_context(argc, argv);
    const auto graph_result =
        actual_context.create_phase_graph(phase_graph_specification(), rift::interface_compatibility::accept_all);

    static rift::RiftContext* context_ptr = nullptr;
    static bool graph_created = false;
    context_ptr = &actual_context;
    graph_created = graph_result.has_value();

    [[maybe_unused]] const suite<"FieldGroupSpaceAgreement"> suite = [] {
        auto& context = *context_ptr;
        "distributed spaces agree on owner and ghost active elements"_test = [&context] {
            expect(graph_created);
            test_distributed_construction(context);
        };
        "three-dimensional distributed active elements match geometric support"_test = [&context] {
            test_distributed_construction_3d(context);
        };
        "numbering policy must agree before construction"_test = [&context] { test_numbering_mismatch(context); };
        "space epoch must agree before construction"_test = [&context] { test_epoch_mismatch(context); };
        "inactive draft state is diagnosed collectively"_test = [&context] { test_inactive_state_mismatch(context); };
        "built draft state is diagnosed collectively"_test = [&context] { test_built_state_mismatch<2>(context, 2); };
        "three-dimensional built state is diagnosed collectively"_test = [&context] {
            test_built_state_mismatch<3>(context, 0);
        };
    };

    const auto result = static_cast<int>(cfg<>.run());
    context_ptr = nullptr;
    graph_created = false;
    return result;
}
