#include <algorithm>
#include <boost/ut.hpp>
#include <cstddef>
#include <cstdint>
#include <deal.II/base/point.h>
#include <deal.II/base/types.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/fe/mapping_q1.h>
#include <deal.II/grid/cell_id.h>
#include <deal.II/grid/grid_generator.h>
#include <memory>
#include <mpi.h>
#include <ranges>
#include <rift/phase_support.hpp>
#include <rift/rift_context.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] constexpr rift::PhaseId air_phase() noexcept { return rift::PhaseId::from_index(0); }

[[nodiscard]] constexpr rift::PhaseId water_phase() noexcept { return rift::PhaseId::from_index(1); }

[[nodiscard]] bool cell_id_less(const dealii::CellId& left, const dealii::CellId& right) { return left < right; }

[[nodiscard]] rift::PhaseGraphSpecification phase_graph_specification()
{
    return {.phases = {{.name = "water", .physics_key = rift::PhysicsKey{"incompressible"}},
                       {.name = "air", .physics_key = rift::PhysicsKey{"compressible"}}},
            .interfaces = {}};
}

[[nodiscard]] std::vector<rift::PhaseSupportSpecification> empty_support_specifications()
{
    return {{.phase = water_phase(), .requested_cells = {}}, {.phase = air_phase(), .requested_cells = {}}};
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
[[nodiscard]] std::shared_ptr<const rift::MeshSnapshot<dim>> make_uniform_snapshot(rift::RiftContext& context)
{
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    dealii::GridGenerator::hyper_cube(*triangulation);
    triangulation->refine_global(1);
    return publish_snapshot(context, std::move(triangulation));
}

template<int dim> struct AdaptiveMeshFixture {
    std::shared_ptr<const rift::MeshSnapshot<dim>> snapshot;
    dealii::CellId refined_parent;
};

template<int dim> [[nodiscard]] AdaptiveMeshFixture<dim> make_adaptive_snapshot(rift::RiftContext& context)
{
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    std::vector<unsigned int> subdivisions(dim, 1U);
    subdivisions.front() = 2U;
    dealii::Point<dim> upper_corner;
    for (unsigned int direction = 0; direction < dim; ++direction) {
        upper_corner[direction] = direction == 0 ? 2.0 : 1.0;
    }
    dealii::GridGenerator::subdivided_hyper_rectangle(*triangulation, subdivisions, dealii::Point<dim>{}, upper_corner);

    for (const auto& cell : triangulation->active_cell_iterators()) {
        if (cell->center()[0] < 1.0) {
            cell->set_refine_flag();
        }
    }
    triangulation->execute_coarsening_and_refinement();

    dealii::CellId refined_parent;
    for (const auto& cell : triangulation->active_cell_iterators()) {
        if (cell->center()[0] < 1.0 && cell->level() > 0) {
            refined_parent = cell->parent()->id();
            break;
        }
    }
    return {.snapshot = publish_snapshot(context, std::move(triangulation)),
            .refined_parent = std::move(refined_parent)};
}

template<int dim>
[[nodiscard]] std::vector<dealii::CellId> locally_owned_active_cells(const rift::MeshSnapshot<dim>& snapshot)
{
    std::vector<dealii::CellId> cells;
    for (const auto& cell : snapshot.triangulation().active_cell_iterators()) {
        if (cell->is_locally_owned()) {
            cells.push_back(cell->id());
        }
    }
    std::ranges::sort(cells, cell_id_less);
    return cells;
}

template<int dim> [[nodiscard]] std::vector<dealii::CellId> hanging_fine_cells(const rift::MeshSnapshot<dim>& snapshot)
{
    std::vector<dealii::CellId> cells;
    for (const auto& cell : snapshot.triangulation().active_cell_iterators()) {
        const auto touches_coarser_neighbor = std::ranges::any_of(cell->face_indices(), [&](const unsigned int face) {
            return !cell->at_boundary(face) && cell->neighbor_is_coarser(face);
        });
        if (touches_coarser_neighbor) {
            cells.push_back(cell->id());
        }
    }
    std::ranges::sort(cells, cell_id_less);
    return cells;
}

void expect_cells(const std::span<const dealii::CellId> actual, const std::vector<dealii::CellId>& expected)
{
    boost::ut::expect(std::ranges::equal(actual, expected));
}

template<class Result>
void expect_error_codes(const Result& result, const std::vector<rift::PhaseSupportErrorCode>& expected_codes)
{
    boost::ut::expect(not result.has_value());
    if (result.has_value()) {
        return;
    }

    boost::ut::expect(result.error().size() == expected_codes.size());
    const auto compared = std::min(result.error().size(), expected_codes.size());
    for (std::size_t index = 0; index < compared; ++index) {
        boost::ut::expect(result.error()[index].code == expected_codes[index]);
        boost::ut::expect(result.error()[index].rank == 0U);
        boost::ut::expect(result.error()[index].message.contains("rank 0"));
    }
}

static_assert(not std::is_copy_constructible_v<rift::PhaseSupport>);
static_assert(not std::is_copy_assignable_v<rift::PhaseSupport>);
static_assert(std::is_nothrow_move_constructible_v<rift::PhaseSupport>);
static_assert(std::is_nothrow_move_assignable_v<rift::PhaseSupport>);
static_assert(not std::is_copy_constructible_v<rift::PhaseSupportSet<2>>);
static_assert(not std::is_copy_assignable_v<rift::PhaseSupportSet<2>>);
static_assert(std::is_nothrow_move_constructible_v<rift::PhaseSupportSet<2>>);
static_assert(std::is_nothrow_move_assignable_v<rift::PhaseSupportSet<2>>);

} // namespace

int main(int argc, char** argv)
{
    using namespace boost::ut;

    rift::RiftContext actual_context(argc, argv);
    const auto graph_result =
        actual_context.create_phase_graph(phase_graph_specification(), rift::interface_compatibility::accept_all);

    // Boost.UT suites cannot capture runtime state during static registration.
    static rift::RiftContext* context_ptr = nullptr;
    static bool graph_created = false;
    context_ptr = &actual_context;
    graph_created = graph_result.has_value();

    [[maybe_unused]] const suite<"PhaseSupport"> suite = [] {
        auto& context = *context_ptr;

        "empty specifications publish one canonical support per phase in 3D"_test = [&context] {
            expect(graph_created);
            const auto snapshot = make_uniform_snapshot<3>(context);
            if (snapshot == nullptr) {
                return;
            }

            const auto result = context.create_phase_supports<3>(snapshot, empty_support_specifications());
            expect(result.has_value());
            if (!result.has_value()) {
                return;
            }

            expect(&result->mesh_snapshot() == snapshot.get());
            expect(result->supports().size() == std::size_t{2});
            expect(result->supports()[0].phase_id() == air_phase());
            expect(result->supports()[1].phase_id() == water_phase());
            for (const auto& support : result->supports()) {
                expect(support.requested_cells().empty());
                expect(support.closure_added_cells().empty());
                expect(support.closed_cells().empty());
            }
            expect(result->support(air_phase()).closed_cells().empty());
        };

        "uniform support sorts requests and retains its mesh owner"_test = [&context] {
            auto snapshot = make_uniform_snapshot<2>(context);
            if (snapshot == nullptr) {
                return;
            }
            const auto snapshot_address = snapshot.get();
            const auto active_cells = locally_owned_active_cells(*snapshot);
            expect(active_cells.size() == std::size_t{4});
            if (active_cells.size() < 4) {
                return;
            }

            const std::vector expected_requested{active_cells.front(), active_cells.back()};
            std::vector<rift::PhaseSupportSpecification> specifications{
                {.phase = water_phase(), .requested_cells = {active_cells.back(), active_cells.front()}},
                {.phase = air_phase(), .requested_cells = {}},
            };
            auto result = context.create_phase_supports<2>(snapshot, std::move(specifications));
            snapshot.reset();

            expect(result.has_value());
            if (!result.has_value()) {
                return;
            }

            expect(&result->mesh_snapshot() == snapshot_address);
            expect(result->supports()[0].phase_id() == air_phase());
            expect(result->supports()[1].phase_id() == water_phase());
            expect_cells(result->support(water_phase()).requested_cells(), expected_requested);
            expect(result->support(water_phase()).closure_added_cells().empty());
            expect_cells(result->support(water_phase()).closed_cells(), expected_requested);
            expect(throws<std::out_of_range>(
                [&result] { static_cast<void>(result->support(rift::PhaseId::from_index(2))); }));
        };

        "one hanging-face request closes the complete fine-side group"_test = [&context] {
            const auto fixture = make_adaptive_snapshot<2>(context);
            if (fixture.snapshot == nullptr) {
                return;
            }
            const auto fine_cells = hanging_fine_cells(*fixture.snapshot);
            expect(fine_cells.size() == std::size_t{2});
            if (fine_cells.size() != 2) {
                return;
            }

            std::vector<rift::PhaseSupportSpecification> specifications{
                {.phase = air_phase(), .requested_cells = {fine_cells.back()}},
                {.phase = water_phase(), .requested_cells = {}},
            };
            const auto result = context.create_phase_supports<2>(fixture.snapshot, std::move(specifications));
            expect(result.has_value());
            if (!result.has_value()) {
                return;
            }

            const std::vector expected_requested{fine_cells.back()};
            const std::vector expected_added{fine_cells.front()};
            const std::vector expected_closed{fine_cells.back(), fine_cells.front()};
            const auto& support = result->support(air_phase());
            expect_cells(support.requested_cells(), expected_requested);
            expect_cells(support.closure_added_cells(), expected_added);
            expect_cells(support.closed_cells(), expected_closed);
            expect(std::ranges::find(support.closed_cells(), fixture.refined_parent) == support.closed_cells().end());
            expect(result->support(water_phase()).closed_cells().empty());
        };

        "3D hanging-face closure adds every fine-side cell"_test = [&context] {
            const auto fixture = make_adaptive_snapshot<3>(context);
            if (fixture.snapshot == nullptr) {
                return;
            }
            const auto fine_cells = hanging_fine_cells(*fixture.snapshot);
            expect(fine_cells.size() == std::size_t{4});
            if (fine_cells.size() != 4) {
                return;
            }

            std::vector<rift::PhaseSupportSpecification> specifications{
                {.phase = air_phase(), .requested_cells = {fine_cells.back()}},
                {.phase = water_phase(), .requested_cells = {}},
            };
            const auto result = context.create_phase_supports<3>(fixture.snapshot, std::move(specifications));
            expect(result.has_value());
            if (!result.has_value()) {
                return;
            }

            const std::vector expected_requested{fine_cells.back()};
            const std::vector<dealii::CellId> expected_added(fine_cells.begin(), fine_cells.end() - 1);
            auto expected_closed = expected_requested;
            expected_closed.insert(expected_closed.end(), expected_added.begin(), expected_added.end());
            const auto& support = result->support(air_phase());
            expect_cells(support.requested_cells(), expected_requested);
            expect_cells(support.closure_added_cells(), expected_added);
            expect_cells(support.closed_cells(), expected_closed);
            expect(std::ranges::find(support.closed_cells(), fixture.refined_parent) == support.closed_cells().end());
        };

        "phase specification validation reports unknown missing and duplicate phases"_test = [&context] {
            const auto snapshot = make_uniform_snapshot<2>(context);
            if (snapshot == nullptr) {
                return;
            }

            std::vector<rift::PhaseSupportSpecification> specifications{
                {.phase = air_phase(), .requested_cells = {}},
                {.phase = rift::PhaseId::from_index(99), .requested_cells = {}},
                {.phase = rift::PhaseId::from_index(99), .requested_cells = {}},
                {.phase = air_phase(), .requested_cells = {}},
            };
            const auto result = context.create_phase_supports<2>(snapshot, std::move(specifications));
            expect_error_codes(result, {rift::PhaseSupportErrorCode::unknown_phase,
                                        rift::PhaseSupportErrorCode::missing_phase_specification,
                                        rift::PhaseSupportErrorCode::duplicate_phase_specification});
        };

        "phase specification validation identifies each omitted canonical phase"_test = [&context] {
            const auto snapshot = make_uniform_snapshot<2>(context);
            if (snapshot == nullptr) {
                return;
            }

            const auto result = context.create_phase_supports<2>(snapshot, {});
            expect_error_codes(result, {rift::PhaseSupportErrorCode::missing_phase_specification,
                                        rift::PhaseSupportErrorCode::missing_phase_specification});
            if (!result.has_value()) {
                expect(result.error()[0].phase == air_phase());
                expect(result.error()[1].phase == water_phase());
            }
        };

        "cell validation reports duplicate absent and inactive requests"_test = [&context] {
            const auto fixture = make_adaptive_snapshot<2>(context);
            if (fixture.snapshot == nullptr) {
                return;
            }
            const auto active_cells = locally_owned_active_cells(*fixture.snapshot);
            expect(not active_cells.empty());
            if (active_cells.empty()) {
                return;
            }

            const dealii::CellId absent_cell{dealii::types::coarse_cell_id{999}, std::vector<std::uint8_t>{}};
            const dealii::CellId locally_absent_cell{dealii::types::coarse_cell_id{0}, std::vector<std::uint8_t>{0, 0}};
            std::vector<rift::PhaseSupportSpecification> specifications{
                {.phase = air_phase(),
                 .requested_cells = {dealii::CellId{}, active_cells.front(), active_cells.front(), active_cells.front(),
                                     locally_absent_cell, absent_cell, fixture.refined_parent}},
                {.phase = water_phase(), .requested_cells = {}},
            };
            const auto result = context.create_phase_supports<2>(fixture.snapshot, std::move(specifications));
            expect_error_codes(result, {rift::PhaseSupportErrorCode::duplicate_requested_cell,
                                        rift::PhaseSupportErrorCode::cell_not_locally_present,
                                        rift::PhaseSupportErrorCode::cell_not_locally_present,
                                        rift::PhaseSupportErrorCode::cell_not_locally_present,
                                        rift::PhaseSupportErrorCode::cell_not_active});
        };

        "3D validation covers malformed absent inactive and duplicate cells"_test = [&context] {
            const auto fixture = make_adaptive_snapshot<3>(context);
            if (fixture.snapshot == nullptr) {
                return;
            }

            const auto active_cells = locally_owned_active_cells(*fixture.snapshot);
            expect(not active_cells.empty());
            if (active_cells.empty()) {
                return;
            }

            const dealii::CellId invalid_child{dealii::types::coarse_cell_id{0}, std::vector<std::uint8_t>{8}};
            const dealii::CellId absent_cell{dealii::types::coarse_cell_id{999}, std::vector<std::uint8_t>{}};
            const dealii::CellId locally_absent_cell{dealii::types::coarse_cell_id{0}, std::vector<std::uint8_t>{0, 0}};
            std::vector<rift::PhaseSupportSpecification> specifications{
                {.phase = air_phase(),
                 .requested_cells = {dealii::CellId{}, dealii::CellId{}, active_cells.front(), active_cells.front(),
                                     active_cells.front(), locally_absent_cell, invalid_child, absent_cell,
                                     fixture.refined_parent}},
                {.phase = water_phase(), .requested_cells = {}},
            };
            const auto result = context.create_phase_supports<3>(fixture.snapshot, std::move(specifications));
            expect_error_codes(result, {rift::PhaseSupportErrorCode::duplicate_requested_cell,
                                        rift::PhaseSupportErrorCode::duplicate_requested_cell,
                                        rift::PhaseSupportErrorCode::cell_not_locally_present,
                                        rift::PhaseSupportErrorCode::cell_not_locally_present,
                                        rift::PhaseSupportErrorCode::cell_not_locally_present,
                                        rift::PhaseSupportErrorCode::cell_not_locally_present,
                                        rift::PhaseSupportErrorCode::cell_not_active});
            if (!result.has_value()) {
                const auto invalid_messages = std::ranges::count_if(
                    result.error(), [](const auto& error) { return error.message.contains("<invalid CellId>"); });
                expect(invalid_messages == std::ptrdiff_t{3});
            }
        };

        "3D null mesh validation still inspects requested identifiers"_test = [&context] {
            auto specifications = empty_support_specifications();
            specifications.back().requested_cells.emplace_back(dealii::types::coarse_cell_id{0},
                                                               std::vector<std::uint8_t>{});
            const auto result = context.create_phase_supports<3>({}, std::move(specifications));
            expect_error_codes(result, {rift::PhaseSupportErrorCode::null_mesh});
        };

        "3D malformed identifiers use their safe diagnostic placeholder"_test = [&context] {
            const auto snapshot = make_uniform_snapshot<3>(context);
            if (snapshot == nullptr) {
                return;
            }

            const dealii::CellId invalid_child{dealii::types::coarse_cell_id{0}, std::vector<std::uint8_t>{8}};
            std::vector<rift::PhaseSupportSpecification> specifications{
                {.phase = air_phase(), .requested_cells = {invalid_child}},
                {.phase = water_phase(), .requested_cells = {}},
            };
            const auto result = context.create_phase_supports<3>(snapshot, std::move(specifications));
            expect_error_codes(result, {rift::PhaseSupportErrorCode::cell_not_locally_present});
            if (!result.has_value()) {
                expect(result.error().front().message.contains("<invalid CellId>"));
            }
        };

        "2D malformed identifiers use their safe diagnostic placeholder"_test = [&context] {
            const auto snapshot = make_uniform_snapshot<2>(context);
            if (snapshot == nullptr) {
                return;
            }

            std::vector<rift::PhaseSupportSpecification> specifications{
                {.phase = air_phase(), .requested_cells = {dealii::CellId{}}},
                {.phase = water_phase(), .requested_cells = {}},
            };
            const auto result = context.create_phase_supports<2>(snapshot, std::move(specifications));
            expect_error_codes(result, {rift::PhaseSupportErrorCode::cell_not_locally_present});
            if (!result.has_value()) {
                expect(result.error().front().message.contains("<invalid CellId>"));
            }
        };

        "duplicate invalid 3D identifiers are reported once per defect"_test = [&context] {
            const auto snapshot = make_uniform_snapshot<3>(context);
            if (snapshot == nullptr) {
                return;
            }

            std::vector<rift::PhaseSupportSpecification> specifications{
                {.phase = air_phase(), .requested_cells = {dealii::CellId{}, dealii::CellId{}}},
                {.phase = water_phase(), .requested_cells = {}},
            };
            const auto result = context.create_phase_supports<3>(snapshot, std::move(specifications));
            expect_error_codes(result, {rift::PhaseSupportErrorCode::duplicate_requested_cell,
                                        rift::PhaseSupportErrorCode::cell_not_locally_present});
        };

        "null mesh validation preserves otherwise valid phase specifications"_test = [&context] {
            auto specifications = empty_support_specifications();
            specifications.back().requested_cells.emplace_back(dealii::types::coarse_cell_id{0},
                                                               std::vector<std::uint8_t>{});
            const auto result = context.create_phase_supports<2>({}, std::move(specifications));
            expect_error_codes(result, {rift::PhaseSupportErrorCode::null_mesh});
        };
    };

    const auto result = static_cast<int>(cfg<>.run());
    context_ptr = nullptr;
    graph_created = false;
    return result;
}
