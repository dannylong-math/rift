#include <algorithm>
#include <boost/ut.hpp>
#include <cstddef>
#include <deal.II/distributed/tria.h>
#include <deal.II/fe/mapping_q1.h>
#include <deal.II/grid/grid_generator.h>
#include <memory>
#include <mpi.h>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/phase_support.hpp>
#include <rift/rift_context.hpp>
#include <rift/space_draft.hpp>
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

[[nodiscard]] std::vector<rift::PhaseSupportSpecification> support_specifications()
{
    return {{.phase = water_phase(), .requested_cells = {}}, {.phase = air_phase(), .requested_cells = {}}};
}

template<int dim> [[nodiscard]] std::shared_ptr<const rift::MeshSnapshot<dim>> make_snapshot(rift::RiftContext& context)
{
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    dealii::GridGenerator::hyper_cube(*triangulation);
    auto result =
        context.create_mesh_snapshot<dim>(std::move(triangulation), std::make_unique<dealii::MappingQ1<dim>>());
    boost::ut::expect(result.has_value());
    return result.has_value() ? *result : nullptr;
}

template<int dim>
[[nodiscard]] rift::PhaseSupportSet<dim> make_supports(rift::RiftContext& context,
                                                       const std::shared_ptr<const rift::MeshSnapshot<dim>>& snapshot)
{
    auto result = context.create_phase_supports<dim>(snapshot, support_specifications());
    boost::ut::expect(result.has_value());
    return std::move(result).value();
}

[[nodiscard]] rift::SpaceSpecification schema(const bool reverse = false)
{
    rift::SpaceSpecification specification{
        .phase_support_fields =
            {
                {.phase = air_phase(), .name = "velocity", .component_count = 2, .degree = 2},
                {.phase = water_phase(), .name = "pressure", .component_count = 1, .degree = 1},
            },
        .geometry = {.continuous_fields = {{.name = "potentials",
                                            .components =
                                                rift::PhaseBoundFieldComponents{.phases = {air_phase(), water_phase()}},
                                            .degree = 2}},
                     .discrete_metadata = {}}};
    if (reverse) {
        std::ranges::reverse(specification.phase_support_fields);
        auto& phases =
            std::get<rift::PhaseBoundFieldComponents>(specification.geometry.continuous_fields.front().components)
                .phases;
        std::ranges::reverse(phases);
    }
    return specification;
}

[[nodiscard]] std::size_t count_code(const rift::SpaceDraftErrors& errors, const rift::SpaceDraftErrorCode code)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(errors, [code](const rift::SpaceDraftError& error) { return error.code == code; }));
}

void test_equivalent_permutations(rift::RiftContext& context)
{
    const auto snapshot = make_snapshot<2>(context);
    if (snapshot == nullptr) {
        return;
    }
    const auto result =
        context.create_space_draft<2>(make_supports(context, snapshot), schema(context.this_mpi_process() % 2U == 1U));
    boost::ut::expect(result.has_value());
    if (result.has_value()) {
        const auto fields = result->canonical_schema().phase_support_fields();
        boost::ut::expect(fields.size() == std::size_t{2});
        if (fields.size() == 2) {
            boost::ut::expect(fields.front().phase == air_phase());
            boost::ut::expect(fields.back().phase == water_phase());
        }
    }
}

void test_schema_mismatch(rift::RiftContext& context)
{
    const auto snapshot = make_snapshot<2>(context);
    if (snapshot == nullptr) {
        return;
    }
    auto specification = schema();
    if (context.this_mpi_process() != 0) {
        specification.geometry.continuous_fields.front().degree = 3;
    }
    const auto result = context.create_space_draft<2>(make_supports(context, snapshot), std::move(specification));
    if (context.n_mpi_processes() == 1) {
        boost::ut::expect(result.has_value());
        return;
    }
    boost::ut::expect(not result.has_value());
    if (!result.has_value()) {
        boost::ut::expect(count_code(result.error(), rift::SpaceDraftErrorCode::schema_mismatch) ==
                          context.n_mpi_processes() - 1U);
    }
}

void test_support_and_mesh_mismatch(rift::RiftContext& context)
{
    const auto first_snapshot = make_snapshot<2>(context);
    const auto second_snapshot = make_snapshot<2>(context);
    if (first_snapshot == nullptr || second_snapshot == nullptr) {
        return;
    }
    auto first = make_supports(context, first_snapshot);
    auto second = make_supports(context, second_snapshot);
    const auto rank = context.this_mpi_process();
    auto selected = rank == 0 ? std::move(first) : std::move(second);
    const auto result = context.create_space_draft<2>(std::move(selected), schema());
    if (context.n_mpi_processes() == 1) {
        boost::ut::expect(result.has_value());
        return;
    }
    boost::ut::expect(not result.has_value());
    if (!result.has_value()) {
        boost::ut::expect(count_code(result.error(), rift::SpaceDraftErrorCode::phase_support_set_mismatch) ==
                          context.n_mpi_processes() - 1U);
        boost::ut::expect(count_code(result.error(), rift::SpaceDraftErrorCode::mesh_snapshot_mismatch) ==
                          context.n_mpi_processes() - 1U);
    }
}

void test_dimension_mismatch(rift::RiftContext& context)
{
    const auto snapshot_2d = make_snapshot<2>(context);
    const auto snapshot_3d = make_snapshot<3>(context);
    if (snapshot_2d == nullptr || snapshot_3d == nullptr) {
        return;
    }
    auto supports_2d = make_supports(context, snapshot_2d);
    auto supports_3d = make_supports(context, snapshot_3d);
    if (context.n_mpi_processes() == 1 || context.this_mpi_process() == 0) {
        const auto result = context.create_space_draft<2>(std::move(supports_2d), schema());
        if (context.n_mpi_processes() == 1) {
            boost::ut::expect(result.has_value());
        }
        else {
            boost::ut::expect(not result.has_value());
            if (!result.has_value()) {
                boost::ut::expect(count_code(result.error(), rift::SpaceDraftErrorCode::dimension_mismatch) ==
                                  context.n_mpi_processes() - 1U);
            }
        }
    }
    else {
        const auto result = context.create_space_draft<3>(std::move(supports_3d), schema());
        boost::ut::expect(not result.has_value());
        if (!result.has_value()) {
            boost::ut::expect(count_code(result.error(), rift::SpaceDraftErrorCode::dimension_mismatch) ==
                              context.n_mpi_processes() - 1U);
        }
    }
}

void test_collective_local_error(rift::RiftContext& context)
{
    const auto snapshot = make_snapshot<2>(context);
    if (snapshot == nullptr) {
        return;
    }
    auto specification = schema();
    if (context.this_mpi_process() == 0) {
        specification.phase_support_fields.front().component_count = 0;
    }
    const auto result = context.create_space_draft<2>(make_supports(context, snapshot), std::move(specification));
    boost::ut::expect(not result.has_value());
    if (!result.has_value()) {
        boost::ut::expect(count_code(result.error(), rift::SpaceDraftErrorCode::zero_component_count) == 1U);
        boost::ut::expect(result.error().front().rank == 0U);
    }
}

void test_collective_inactive_support(rift::RiftContext& context)
{
    const auto snapshot = make_snapshot<2>(context);
    if (snapshot == nullptr) {
        return;
    }
    auto moved_from = make_supports(context, snapshot);
    auto active = std::move(moved_from);
    // Rank zero intentionally contributes the moved-from support aggregate.
    auto selected = context.this_mpi_process() == 0 // NOLINT(bugprone-use-after-move)
                        ? std::move(moved_from)
                        : std::move(active);
    const auto result = context.create_space_draft<2>(std::move(selected), schema());
    boost::ut::expect(not result.has_value());
    if (!result.has_value()) {
        boost::ut::expect(count_code(result.error(), rift::SpaceDraftErrorCode::inactive_phase_support_set) == 1U);
        const auto inactive = std::ranges::find_if(result.error(), [](const auto& error) {
            return error.code == rift::SpaceDraftErrorCode::inactive_phase_support_set;
        });
        boost::ut::expect(inactive != result.error().end());
        if (inactive != result.error().end()) {
            boost::ut::expect(inactive->rank == 0U);
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    using namespace boost::ut;

    rift::RiftContext actual_context(argc, argv);
    const auto graph_result =
        actual_context.create_phase_graph(phase_graph_specification(), rift::interface_compatibility::accept_all);

    static rift::RiftContext* context_ptr = nullptr;
    static bool graph_created = false;
    context_ptr = &actual_context;
    graph_created = graph_result.has_value();

    [[maybe_unused]] const suite<"SpaceDraftAgreement"> suite = [] {
        auto& context = *context_ptr;
        "equivalent schema permutations agree exactly"_test = [&context] {
            expect(graph_created);
            test_equivalent_permutations(context);
        };
        "schema differences return coherent collective diagnostics"_test = [&context] {
            test_schema_mismatch(context);
        };
        "support and mesh provenance must agree"_test = [&context] { test_support_and_mesh_mismatch(context); };
        "space dimension must agree"_test = [&context] { test_dimension_mismatch(context); };
        "one rank's local validation error reaches every rank"_test = [&context] {
            test_collective_local_error(context);
        };
        "one inactive support set fails collectively"_test = [&context] { test_collective_inactive_support(context); };
    };

    const auto result = static_cast<int>(cfg<>.run());
    context_ptr = nullptr;
    graph_created = false;
    return result;
}
