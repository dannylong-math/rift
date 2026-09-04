#include <algorithm>
#include <boost/ut.hpp>
#include <cstddef>
#include <deal.II/base/point.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/dofs/dof_tools.h>
#include <deal.II/fe/fe_data.h>
#include <deal.II/fe/mapping_q1.h>
#include <deal.II/grid/cell_id.h>
#include <deal.II/grid/grid_generator.h>
#include <memory>
#include <mpi.h>
#include <rift/field_group_space.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/phase_support.hpp>
#include <rift/rift_context.hpp>
#include <rift/space_draft.hpp>
#include <stdexcept>
#include <type_traits>
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

template<int dim>
[[nodiscard]] std::shared_ptr<const rift::MeshSnapshot<dim>>
publish_snapshot(rift::RiftContext& context,
                 std::unique_ptr<dealii::parallel::distributed::Triangulation<dim>> triangulation)
{
    auto result =
        context.create_mesh_snapshot<dim>(std::move(triangulation), std::make_unique<dealii::MappingQ1<dim>>());
    boost::ut::expect(result.has_value());
    return result.has_value() ? *result : nullptr;
}

template<int dim>
[[nodiscard]] std::shared_ptr<const rift::MeshSnapshot<dim>> make_uniform_snapshot(rift::RiftContext& context,
                                                                                   const unsigned int refinements)
{
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    dealii::GridGenerator::hyper_cube(*triangulation);
    triangulation->refine_global(refinements);
    return publish_snapshot(context, std::move(triangulation));
}

template<int dim>
[[nodiscard]] std::shared_ptr<const rift::MeshSnapshot<dim>> make_adaptive_snapshot(rift::RiftContext& context)
{
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    std::vector<unsigned int> subdivisions(dim, 1U);
    subdivisions.front() = 2U;
    dealii::Point<dim> upper_corner;
    for (unsigned int direction = 0; std::cmp_less(direction, dim); ++direction) {
        // The loop proves that the Point index is in range.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        upper_corner[direction] = direction == 0 ? 2.0 : 1.0;
    }
    dealii::GridGenerator::subdivided_hyper_rectangle(*triangulation, subdivisions, dealii::Point<dim>{}, upper_corner);
    for (const auto& cell : triangulation->active_cell_iterators()) {
        if (cell->center()[0] < 1.0) { // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
            cell->set_refine_flag();
        }
    }
    triangulation->execute_coarsening_and_refinement();
    return publish_snapshot(context, std::move(triangulation));
}

template<int dim>
[[nodiscard]] std::vector<dealii::CellId> locally_owned_cells(const rift::MeshSnapshot<dim>& snapshot,
                                                              const auto& predicate)
{
    std::vector<dealii::CellId> cells;
    for (const auto& cell : snapshot.triangulation().active_cell_iterators()) {
        if (cell->is_locally_owned() && predicate(cell)) {
            cells.push_back(cell->id());
        }
    }
    std::ranges::sort(cells, [](const dealii::CellId& left, const dealii::CellId& right) { return left < right; });
    return cells;
}

template<int dim>
[[nodiscard]] rift::PhaseSupportResult<dim>
make_supports(rift::RiftContext& context, const std::shared_ptr<const rift::MeshSnapshot<dim>>& snapshot,
              std::vector<dealii::CellId> air_cells)
{
    std::vector<rift::PhaseSupportSpecification> specifications;
    specifications.push_back({.phase = water_phase(), .requested_cells = {}});
    specifications.push_back({.phase = air_phase(), .requested_cells = std::move(air_cells)});
    return context.create_phase_supports<dim>(snapshot, std::move(specifications));
}

[[nodiscard]] rift::SpaceSpecification full_space_specification()
{
    return {.phase_support_fields =
                {
                    {.phase = air_phase(), .name = "velocity", .component_count = 2, .degree = 2},
                    {.phase = air_phase(), .name = "pressure", .component_count = 1, .degree = 1},
                },
            .geometry = {
                .continuous_fields =
                    {
                        {.name = "temperature", .components = rift::UnboundFieldComponents{.count = 1}, .degree = 1},
                        {.name = "phase-potentials",
                         .components = rift::PhaseBoundFieldComponents{.phases = {water_phase(), air_phase()}},
                         .degree = 2},
                    },
                .discrete_metadata = {}}};
}

[[nodiscard]] rift::SpaceSpecification scalar_space_specification()
{
    return {.phase_support_fields = {{.phase = air_phase(), .name = "pressure", .component_count = 1, .degree = 1}},
            .geometry = {.continuous_fields = {{.name = "phase-potentials",
                                                .components = rift::PhaseBoundFieldComponents{.phases = {water_phase(),
                                                                                                         air_phase()}},
                                                .degree = 1}},
                         .discrete_metadata = {}}};
}

[[nodiscard]] std::size_t count_code(const rift::FieldSpaceBuildErrors& errors,
                                     const rift::FieldSpaceBuildErrorCode code)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(errors, [code](const auto& error) { return error.code == code; }));
}

static_assert(not std::is_copy_constructible_v<rift::PhaseSupportFieldGroupSpace<2>>);
static_assert(not std::is_copy_assignable_v<rift::PhaseSupportFieldGroupSpace<2>>);
static_assert(std::is_nothrow_move_constructible_v<rift::PhaseSupportFieldGroupSpace<2>>);
static_assert(not std::is_move_assignable_v<rift::PhaseSupportFieldGroupSpace<2>>);
static_assert(not std::is_copy_constructible_v<rift::GeometryFieldGroupSpace<2>>);
static_assert(not std::is_copy_assignable_v<rift::GeometryFieldGroupSpace<2>>);
static_assert(std::is_nothrow_move_constructible_v<rift::GeometryFieldGroupSpace<2>>);
static_assert(not std::is_move_assignable_v<rift::GeometryFieldGroupSpace<2>>);

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

    [[maybe_unused]] const suite<"FieldGroupSpace"> suite = [] { // NOLINT(readability-function-cognitive-complexity)
        auto& context = *context_ptr;

        "construction publishes canonical spaces with analytic DoF counts"_test = [&context] {
            expect(graph_created);
            const auto snapshot = make_uniform_snapshot<2>(context, 1);
            if (snapshot == nullptr) {
                return;
            }
            auto air_cells = locally_owned_cells(*snapshot, [](const auto&) { return true; });
            expect(air_cells.size() == std::size_t{4});
            if (air_cells.empty()) {
                return;
            }
            air_cells.resize(1);
            const auto supported_cell = air_cells.front();
            auto supports = make_supports(context, snapshot, std::move(air_cells));
            expect(supports.has_value());
            if (!supports.has_value()) {
                return;
            }
            const auto support_id = supports->id();
            auto draft_result = context.create_space_draft<2>(std::move(*supports), full_space_specification());
            expect(draft_result.has_value());
            if (!draft_result.has_value()) {
                return;
            }
            auto& draft = *draft_result;

            expect(not draft.field_spaces_built());
            expect(draft.phase_support_field_spaces().empty());
            expect(draft.geometry_field_spaces().empty());
            expect(throws<std::logic_error>([&draft] {
                static_cast<void>(draft.phase_support_field_space(rift::PhaseSupportFieldGroupId::from_index(0)));
            }));
            expect(throws<std::logic_error>([&draft] {
                static_cast<void>(draft.geometry_field_space(rift::GeometryFieldGroupId::from_index(0)));
            }));

            const auto build = context.build_field_spaces(draft);
            expect(build.has_value());
            if (!build.has_value()) {
                return;
            }

            expect(draft.field_spaces_built());
            const auto support_spaces = draft.phase_support_field_spaces();
            const auto geometry_spaces = draft.geometry_field_spaces();
            expect(support_spaces.size() == std::size_t{2});
            expect(geometry_spaces.size() == std::size_t{2});
            if (support_spaces.size() != 2 || geometry_spaces.size() != 2) {
                return;
            }

            const auto& pressure = support_spaces.front();
            const auto& velocity = support_spaces.back();
            expect(pressure.descriptor().name == "pressure");
            expect(pressure.descriptor().component_count == 1_u);
            expect(pressure.descriptor().degree == 1_u);
            expect(pressure.epoch() == draft.epoch());
            expect(pressure.numbering() == rift::DofNumbering::native);
            expect(pressure.phase_support_set_id() == support_id);
            expect(pressure.phase() == air_phase());
            expect(pressure.finite_elements().size() == std::size_t{2});
            // The size assertion above proves both public fixed indices are valid.
            // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
            const auto& ordinary = pressure.finite_elements()[rift::PhaseSupportFieldGroupSpace<2>::ordinary_fe_index];
            const auto& outside =
                pressure.finite_elements()[rift::PhaseSupportFieldGroupSpace<2>::outside_support_fe_index];
            // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
            expect(ordinary.n_components() == 1_u);
            expect(outside.n_components() == 1_u);
            expect(outside.compare_for_domination(ordinary) == dealii::FiniteElementDomination::no_requirements);
            expect(pressure.dof_handler().n_dofs() == 4_u);
            expect(pressure.constraints().is_closed());
            expect(pressure.constraints().n_constraints() == 0_u);
            expect(pressure.locally_owned_dofs().n_elements() == 4_u);
            expect(pressure.locally_owned_dofs().is_subset_of(pressure.locally_relevant_dofs()));
            for (const auto& cell : pressure.dof_handler().active_cell_iterators()) {
                const auto expected = cell->id() == supported_cell
                                          ? rift::PhaseSupportFieldGroupSpace<2>::ordinary_fe_index
                                          : rift::PhaseSupportFieldGroupSpace<2>::outside_support_fe_index;
                expect(cell->active_fe_index() == expected);
            }

            expect(velocity.descriptor().name == "velocity");
            // Every approved collection contains the fixed ordinary index.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
            expect(velocity.finite_elements()[rift::PhaseSupportFieldGroupSpace<2>::ordinary_fe_index].n_components() ==
                   2_u);
            expect(velocity.dof_handler().n_dofs() == 18_u);

            const auto& potentials = geometry_spaces.front();
            const auto& temperature = geometry_spaces.back();
            expect(potentials.descriptor().name == "phase-potentials");
            expect(std::get<rift::PhaseBoundFieldComponents>(potentials.descriptor().components).phases ==
                   std::vector{air_phase(), water_phase()});
            expect(potentials.epoch() == draft.epoch());
            expect(potentials.numbering() == rift::DofNumbering::native);
            expect(potentials.finite_element().n_components() == 2_u);
            expect(potentials.dof_handler().n_dofs() == 50_u);
            expect(potentials.constraints().is_closed());
            expect(potentials.constraints().n_constraints() == 0_u);
            expect(potentials.locally_owned_dofs().is_subset_of(potentials.locally_relevant_dofs()));
            expect(temperature.descriptor().name == "temperature");
            expect(std::get<rift::UnboundFieldComponents>(temperature.descriptor().components).count == 1_u);
            expect(temperature.dof_handler().n_dofs() == 9_u);

            expect(&draft.phase_support_field_space(pressure.descriptor().id) == &pressure);
            expect(&draft.geometry_field_space(potentials.descriptor().id) == &potentials);
            expect(throws<std::out_of_range>([&draft] {
                static_cast<void>(draft.phase_support_field_space(rift::PhaseSupportFieldGroupId::from_index(9)));
            }));
            expect(throws<std::out_of_range>([&draft] {
                static_cast<void>(draft.geometry_field_space(rift::GeometryFieldGroupId::from_index(9)));
            }));

            const auto rebuild = context.build_field_spaces(draft);
            expect(not rebuild.has_value());
            if (!rebuild.has_value()) {
                expect(count_code(rebuild.error(), rift::FieldSpaceBuildErrorCode::field_spaces_already_built) == 1_u);
            }

            auto active_owner = std::move(draft);
            expect(active_owner.active());
            // Deliberately exercise the documented moved-from preflight path.
            // NOLINTNEXTLINE(bugprone-use-after-move)
            const auto inactive = context.build_field_spaces(draft);
            expect(not inactive.has_value());
            if (!inactive.has_value()) {
                expect(count_code(inactive.error(), rift::FieldSpaceBuildErrorCode::inactive_draft) == 1_u);
            }
        };

        "absent phase support publishes a valid empty field space"_test = [&context] {
            const auto snapshot = make_uniform_snapshot<2>(context, 0);
            if (snapshot == nullptr) {
                return;
            }
            auto supports = make_supports(context, snapshot, {});
            expect(supports.has_value());
            if (!supports.has_value()) {
                return;
            }
            auto draft_result = context.create_space_draft<2>(std::move(*supports), scalar_space_specification());
            expect(draft_result.has_value());
            if (!draft_result.has_value()) {
                return;
            }
            auto& draft = *draft_result;
            const auto build = context.build_field_spaces(draft);
            expect(build.has_value());
            if (!build.has_value()) {
                return;
            }
            const auto& empty_space = draft.phase_support_field_spaces().front();
            expect(empty_space.dof_handler().n_dofs() == 0_u);
            expect(empty_space.locally_owned_dofs().is_empty());
            expect(empty_space.locally_relevant_dofs().is_empty());
            expect(empty_space.constraints().is_closed());
            expect(empty_space.constraints().n_constraints() == 0_u);
        };

        "component-wise numbering groups each component contiguously"_test = [&context] {
            const auto snapshot = make_uniform_snapshot<2>(context, 1);
            if (snapshot == nullptr) {
                return;
            }
            auto air_cells = locally_owned_cells(*snapshot, [](const auto&) { return true; });
            auto supports = make_supports(context, snapshot, std::move(air_cells));
            expect(supports.has_value());
            if (!supports.has_value()) {
                return;
            }
            auto draft_result = context.create_space_draft<2>(std::move(*supports), scalar_space_specification());
            expect(draft_result.has_value());
            if (!draft_result.has_value()) {
                return;
            }
            auto& draft = *draft_result;
            const auto build = context.build_field_spaces(
                draft, rift::FieldSpaceBuildOptions{.numbering = rift::DofNumbering::component_wise});
            expect(build.has_value());
            if (!build.has_value()) {
                return;
            }

            const auto& geometry = draft.geometry_field_spaces().front();
            expect(geometry.numbering() == rift::DofNumbering::component_wise);
            expect(geometry.dof_handler().n_dofs() == 18_u);
            const auto components = dealii::DoFTools::locally_owned_dofs_per_component(geometry.dof_handler());
            expect(components.size() == std::size_t{2});
            if (components.size() == 2) {
                expect(components.front().n_elements() == 9_u);
                expect(components.back().n_elements() == 9_u);
                expect(components.front().is_contiguous());
                expect(components.back().is_contiguous());
                expect(components.front().nth_index_in_set(0) == 0_u);
                expect(components.back().nth_index_in_set(0) == 9_u);
            }
        };

        "adaptive mesh creates only the independently expected constraints"_test = [&context] {
            const auto snapshot = make_adaptive_snapshot<2>(context);
            if (snapshot == nullptr) {
                return;
            }
            auto hanging_face_cells = locally_owned_cells(*snapshot, [](const auto& cell) {
                return std::ranges::any_of(cell->face_indices(), [&](const unsigned int face) {
                    return !cell->at_boundary(face) && cell->neighbor_is_coarser(face);
                });
            });
            expect(hanging_face_cells.size() == std::size_t{2});
            if (hanging_face_cells.empty()) {
                return;
            }
            hanging_face_cells.resize(1);
            auto supports = make_supports(context, snapshot, std::move(hanging_face_cells));
            expect(supports.has_value());
            if (!supports.has_value()) {
                return;
            }
            expect(supports->support(air_phase()).requested_cells().size() == std::size_t{1});
            expect(supports->support(air_phase()).closure_added_cells().size() == std::size_t{1});
            auto draft_result = context.create_space_draft<2>(std::move(*supports), scalar_space_specification());
            expect(draft_result.has_value());
            if (!draft_result.has_value()) {
                return;
            }
            auto& draft = *draft_result;
            const auto build = context.build_field_spaces(draft);
            expect(build.has_value());
            if (!build.has_value()) {
                return;
            }

            const auto& phase_space = draft.phase_support_field_spaces().front();
            expect(phase_space.dof_handler().n_dofs() == 6_u);
            expect(phase_space.constraints().n_constraints() == 0_u);
            for (const auto& cell : phase_space.dof_handler().active_cell_iterators()) {
                const auto on_hanging_face = std::ranges::any_of(cell->face_indices(), [&](const unsigned int face) {
                    return !cell->at_boundary(face) && cell->neighbor_is_coarser(face);
                });
                const auto expected = on_hanging_face ? rift::PhaseSupportFieldGroupSpace<2>::ordinary_fe_index
                                                      : rift::PhaseSupportFieldGroupSpace<2>::outside_support_fe_index;
                expect(cell->active_fe_index() == expected);
            }
            const auto& geometry_space = draft.geometry_field_spaces().front();
            expect(geometry_space.dof_handler().n_dofs() == 22_u);
            expect(geometry_space.constraints().n_constraints() == 2_u);
        };

        "three-dimensional adaptive spaces use the same public contract"_test = [&context] {
            const auto snapshot = make_adaptive_snapshot<3>(context);
            if (snapshot == nullptr) {
                return;
            }
            auto air_cells = locally_owned_cells(*snapshot, [](const auto& cell) {
                return std::ranges::any_of(cell->face_indices(), [&](const unsigned int face) {
                    return !cell->at_boundary(face) && cell->neighbor_is_coarser(face);
                });
            });
            expect(air_cells.size() == std::size_t{4});
            if (air_cells.empty()) {
                return;
            }
            air_cells.resize(1);
            auto supports = make_supports(context, snapshot, std::move(air_cells));
            expect(supports.has_value());
            if (!supports.has_value()) {
                return;
            }
            const auto support_id = supports->id();
            expect(supports->support(air_phase()).requested_cells().size() == std::size_t{1});
            expect(supports->support(air_phase()).closure_added_cells().size() == std::size_t{3});
            auto draft_result = context.create_space_draft<3>(std::move(*supports), scalar_space_specification());
            expect(draft_result.has_value());
            if (!draft_result.has_value()) {
                return;
            }
            auto& draft = *draft_result;
            expect(draft.phase_support_field_spaces().empty());
            expect(draft.geometry_field_spaces().empty());
            expect(throws<std::logic_error>([&draft] {
                static_cast<void>(draft.phase_support_field_space(rift::PhaseSupportFieldGroupId::from_index(0)));
            }));
            expect(throws<std::logic_error>([&draft] {
                static_cast<void>(draft.geometry_field_space(rift::GeometryFieldGroupId::from_index(0)));
            }));

            const auto build = context.build_field_spaces(
                draft, rift::FieldSpaceBuildOptions{.numbering = rift::DofNumbering::component_wise});
            expect(build.has_value());
            if (!build.has_value()) {
                return;
            }

            const auto& phase_space = draft.phase_support_field_spaces().front();
            expect(phase_space.descriptor().name == "pressure");
            expect(phase_space.epoch() == draft.epoch());
            expect(phase_space.numbering() == rift::DofNumbering::component_wise);
            expect(phase_space.phase_support_set_id() == support_id);
            expect(phase_space.phase() == air_phase());
            expect(phase_space.finite_elements().size() == std::size_t{2});
            expect(phase_space.dof_handler().n_dofs() == 18_u);
            expect(phase_space.constraints().is_closed());
            expect(phase_space.constraints().n_constraints() == 0_u);
            expect(phase_space.locally_owned_dofs().is_subset_of(phase_space.locally_relevant_dofs()));

            const auto& geometry_space = draft.geometry_field_spaces().front();
            expect(geometry_space.descriptor().name == "phase-potentials");
            expect(geometry_space.epoch() == draft.epoch());
            expect(geometry_space.numbering() == rift::DofNumbering::component_wise);
            expect(geometry_space.finite_element().n_components() == 2_u);
            expect(geometry_space.dof_handler().n_dofs() == 62_u);
            expect(geometry_space.constraints().is_closed());
            expect(geometry_space.constraints().n_constraints() == 10_u);
            expect(geometry_space.locally_owned_dofs().is_subset_of(geometry_space.locally_relevant_dofs()));

            expect(&draft.phase_support_field_space(phase_space.descriptor().id) == &phase_space);
            expect(&draft.geometry_field_space(geometry_space.descriptor().id) == &geometry_space);
            expect(throws<std::out_of_range>([&draft] {
                static_cast<void>(draft.phase_support_field_space(rift::PhaseSupportFieldGroupId::from_index(9)));
            }));
            expect(throws<std::out_of_range>([&draft] {
                static_cast<void>(draft.geometry_field_space(rift::GeometryFieldGroupId::from_index(9)));
            }));

            const auto rebuild = context.build_field_spaces(draft);
            expect(not rebuild.has_value());
            if (!rebuild.has_value()) {
                expect(count_code(rebuild.error(), rift::FieldSpaceBuildErrorCode::field_spaces_already_built) == 1_u);
            }

            auto active_owner = std::move(draft);
            expect(active_owner.active());
            // Deliberately exercise the 3D moved-from preflight path.
            // NOLINTNEXTLINE(bugprone-use-after-move)
            const auto inactive = context.build_field_spaces(draft);
            expect(not inactive.has_value());
            if (!inactive.has_value()) {
                expect(count_code(inactive.error(), rift::FieldSpaceBuildErrorCode::inactive_draft) == 1_u);
            }
        };

        "moving a built draft transfers stable field-space ownership"_test = [&context] {
            const auto snapshot = make_uniform_snapshot<2>(context, 0);
            if (snapshot == nullptr) {
                return;
            }
            auto make_built_draft = [&context, &snapshot]() {
                auto air_cells = locally_owned_cells(*snapshot, [](const auto&) { return true; });
                auto supports = make_supports(context, snapshot, std::move(air_cells));
                auto draft = context.create_space_draft<2>(std::move(*supports), scalar_space_specification());
                boost::ut::expect(draft.has_value());
                if (draft.has_value()) {
                    boost::ut::expect(context.build_field_spaces(*draft).has_value());
                }
                return draft;
            };
            auto first = make_built_draft();
            auto second = make_built_draft();
            if (!first.has_value() || !second.has_value()) {
                return;
            }
            const auto second_epoch = second->epoch();
            *first = std::move(*second);
            expect(first->active());
            expect(first->field_spaces_built());
            expect(first->epoch() == second_epoch);
            expect(not second->active());
            expect(first->phase_support_field_spaces().front().epoch() == second_epoch);
        };
    };

    const auto result = static_cast<int>(cfg<>.run());
    context_ptr = nullptr;
    graph_created = false;
    return result;
}
