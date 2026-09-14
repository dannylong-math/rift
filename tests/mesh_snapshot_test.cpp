#include <boost/ut.hpp>
#include <cstddef>
#include <cstdint>
#include <deal.II/base/point.h>
#include <deal.II/base/types.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/fe/mapping.h>
#include <deal.II/fe/mapping_q1.h>
#include <deal.II/grid/grid_generator.h>
#include <memory>
#include <mpi.h>
#include <rift/mesh_snapshot.hpp>
#include <rift/rift_context.hpp>
#include <type_traits>
#include <utility>

namespace {

template<int dim>
[[nodiscard]] std::unique_ptr<dealii::parallel::distributed::Triangulation<dim>>
make_triangulation(const MPI_Comm communicator = MPI_COMM_WORLD)
{
    return std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(communicator);
}

template<int dim> [[nodiscard]] std::unique_ptr<dealii::Mapping<dim>> make_mapping()
{
    return std::make_unique<dealii::MappingQ1<dim>>();
}

template<int dim>
concept HasMeshSnapshot = requires { typename rift::MeshSnapshot<dim>; };

static_assert(HasMeshSnapshot<2>);
static_assert(HasMeshSnapshot<3>);
static_assert(not HasMeshSnapshot<1>);
static_assert(not std::is_copy_constructible_v<rift::MeshSnapshot<2>>);
static_assert(not std::is_copy_assignable_v<rift::MeshSnapshot<2>>);
static_assert(not std::is_move_constructible_v<rift::MeshSnapshot<2>>);
static_assert(not std::is_move_assignable_v<rift::MeshSnapshot<2>>);
static_assert(std::is_same_v<decltype(std::declval<const rift::MeshSnapshot<2>&>().triangulation()),
                             const dealii::parallel::distributed::Triangulation<2>&>);
static_assert(
    std::is_same_v<decltype(std::declval<const rift::MeshSnapshot<2>&>().mapping()), const dealii::Mapping<2>&>);

} // namespace

int main(int argc, char** argv)
{
    using namespace boost::ut;

    rift::RiftContext actual_context(argc, argv);

    // Boost.UT suites cannot capture runtime state during static registration.
    static rift::RiftContext* context_ptr = nullptr;
    context_ptr = &actual_context;

    [[maybe_unused]] const suite<"MeshSnapshot"> suite = [] {
        auto& context = *context_ptr;

        "MeshSnapshot adopts a populated triangulation and mapping"_test = [&context] {
            auto triangulation = make_triangulation<2>();
            dealii::GridGenerator::hyper_cube(*triangulation, 2.0, 4.0);
            const auto* const triangulation_address = triangulation.get();

            auto mapping = make_mapping<2>();
            const auto* const mapping_address = mapping.get();

            const auto result = context.create_mesh_snapshot<2>(std::move(triangulation), std::move(mapping));
            expect(result.has_value());
            if (!result.has_value()) {
                return;
            }

            const auto& snapshot = **result;
            expect(&snapshot.triangulation() == triangulation_address);
            expect(&snapshot.mapping() == mapping_address);
            expect(snapshot.communicator() == MPI_COMM_WORLD);
            expect(snapshot.triangulation().n_global_active_cells() == dealii::types::global_cell_index{1});

            const dealii::Point<2> unit_point(0.25, 0.75);
            const auto real_point =
                snapshot.mapping().transform_unit_to_real_cell(snapshot.triangulation().begin(0), unit_point);
            expect(real_point == dealii::Point<2>(2.5, 3.5));
        };

        "MeshSnapshot accepts an empty distributed triangulation"_test = [&context] {
            const auto result = context.create_mesh_snapshot<3>(make_triangulation<3>(), make_mapping<3>());
            expect(result.has_value());
            if (result.has_value()) {
                expect((*result)->triangulation().n_global_active_cells() == dealii::types::global_cell_index{0});
            }
        };

        "MeshSnapshot reports ordered local input errors"_test = [&context] {
            const auto result = context.create_mesh_snapshot<2>({}, {});
            expect(not result.has_value());
            if (result.has_value()) {
                return;
            }

            const auto& errors = result.error();
            expect(errors.size() == std::size_t{2});
            if (errors.size() != 2) {
                return;
            }
            expect(errors.at(0).code == rift::MeshSnapshotErrorCode::null_triangulation);
            expect(errors.at(0).rank == 0_u);
            expect(errors.at(0).message.contains("rank 0"));
            expect(errors.at(1).code == rift::MeshSnapshotErrorCode::null_mapping);
            expect(errors.at(1).rank == 0_u);
            expect(errors.at(1).message.contains("rank 0"));
        };

        "MeshSnapshot validates null resources for the 3D instantiation"_test = [&context] {
            const auto result = context.create_mesh_snapshot<3>({}, make_mapping<3>());
            expect(not result.has_value());
            if (!result.has_value()) {
                expect(result.error().size() == std::size_t{1});
                expect(result.error().front().code == rift::MeshSnapshotErrorCode::null_triangulation);
            }
        };

        "MeshSnapshot errors compare every diagnostic field"_test = [] {
            const rift::MeshSnapshotError baseline{
                .code = rift::MeshSnapshotErrorCode::null_triangulation, .rank = 1, .message = "diagnostic"};
            const rift::MeshSnapshotError equal{
                .code = rift::MeshSnapshotErrorCode::null_triangulation, .rank = 1, .message = "diagnostic"};
            const rift::MeshSnapshotError different_code{
                .code = rift::MeshSnapshotErrorCode::null_mapping, .rank = 1, .message = "diagnostic"};
            const rift::MeshSnapshotError different_rank{
                .code = rift::MeshSnapshotErrorCode::null_triangulation, .rank = 2, .message = "diagnostic"};
            const rift::MeshSnapshotError different_message{
                .code = rift::MeshSnapshotErrorCode::null_triangulation, .rank = 1, .message = "other"};

            expect(baseline == equal);
            expect(baseline != different_code);
            expect(baseline != different_rank);
            expect(baseline != different_message);
        };

        "MeshSnapshot rejects a non-world communicator"_test = [&context] {
            const auto result =
                context.create_mesh_snapshot<2>(make_triangulation<2>(MPI_COMM_SELF), make_mapping<2>());
            expect(not result.has_value());
            if (result.has_value()) {
                return;
            }

            expect(result.error().size() == std::size_t{1});
            if (result.error().size() == 1) {
                const auto& error = result.error().front();
                expect(error.code == rift::MeshSnapshotErrorCode::communicator_mismatch);
                expect(error.rank == 0_u);
                expect(error.message.contains("MPI_COMM_WORLD"));
            }
        };

        "MeshSnapshot IDs advance only after successful publication"_test = [&context] {
            const auto first = context.create_mesh_snapshot<2>(make_triangulation<2>(), make_mapping<2>());
            expect(first.has_value());

            const auto failed = context.create_mesh_snapshot<2>({}, make_mapping<2>());
            expect(not failed.has_value());

            const auto second = context.create_mesh_snapshot<3>(make_triangulation<3>(), make_mapping<3>());
            expect(second.has_value());
            if (first.has_value() && second.has_value()) {
                expect((*second)->id().value() == (*first)->id().value() + std::uint64_t{1});
            }
        };
    };

    const auto result = static_cast<int>(cfg<>.run());
    context_ptr = nullptr;
    return result;
}
