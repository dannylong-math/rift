#include <algorithm>
#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/fe/mapping.h>
#include <deal.II/fe/mapping_q1.h>
#include <memory>
#include <mpi.h>
#include <rift/mesh_snapshot.hpp>
#include <rift/rift_context.hpp>
#include <string>
#include <utility>
#include <vector>

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

[[nodiscard]] rift::MeshSnapshotError make_error(const rift::MeshSnapshotErrorCode code, const unsigned int rank,
                                                 std::string message)
{
    return {.code = code, .rank = rank, .message = std::move(message)};
}

template<class Result> void expect_errors(const Result& result, const rift::MeshSnapshotErrors& expected)
{
    boost::ut::expect(not result.has_value());
    if (!result.has_value()) {
        boost::ut::expect(result.error() == expected);
    }
}

void test_asymmetric_null_resources(rift::RiftContext& context)
{
    auto triangulation = make_triangulation<2>();
    auto mapping = make_mapping<2>();
    const auto rank = context.this_mpi_process();
    const auto size = context.n_mpi_processes();

    if (rank == 0U) {
        triangulation.reset();
    }
    if (rank + 1U == size) {
        mapping.reset();
    }

    const auto result = context.create_mesh_snapshot<2>(std::move(triangulation), std::move(mapping));

    rift::MeshSnapshotErrors expected;
    for (unsigned int error_rank = 0; error_rank < size; ++error_rank) {
        if (error_rank == 0U) {
            expected.push_back(make_error(rift::MeshSnapshotErrorCode::null_triangulation, error_rank,
                                          "rank 0 supplied no distributed triangulation"));
        }
        if (error_rank + 1U == size) {
            expected.push_back(make_error(rift::MeshSnapshotErrorCode::null_mapping, error_rank,
                                          "rank " + std::to_string(error_rank) + " supplied no mapping"));
        }
    }
    expect_errors(result, expected);
}

void test_non_world_communicators(rift::RiftContext& context)
{
    const auto result = context.create_mesh_snapshot<2>(make_triangulation<2>(MPI_COMM_SELF), make_mapping<2>());

    rift::MeshSnapshotErrors expected;
    for (unsigned int rank = 0; rank < context.n_mpi_processes(); ++rank) {
        expected.push_back(make_error(rift::MeshSnapshotErrorCode::communicator_mismatch, rank,
                                      "rank " + std::to_string(rank) + " triangulation does not use MPI_COMM_WORLD"));
    }
    expect_errors(result, expected);
}

void test_dimension_agreement(rift::RiftContext& context)
{
    const auto rank = context.this_mpi_process();
    const auto size = context.n_mpi_processes();
    if (size == 1U) {
        const auto result = context.create_mesh_snapshot<2>(make_triangulation<2>(), make_mapping<2>());
        boost::ut::expect(result.has_value());
        return;
    }

    rift::MeshSnapshotErrors expected;
    for (unsigned int error_rank = 1; error_rank < size; ++error_rank) {
        expected.push_back(
            make_error(rift::MeshSnapshotErrorCode::dimension_mismatch, error_rank,
                       "rank " + std::to_string(error_rank) + " requested mesh dimension 3, but rank 0 requested 2"));
    }

    if (rank == 0U) {
        const auto result = context.create_mesh_snapshot<2>(make_triangulation<2>(), make_mapping<2>());
        expect_errors(result, expected);
    }
    else {
        const auto result = context.create_mesh_snapshot<3>(make_triangulation<3>(), make_mapping<3>());
        expect_errors(result, expected);
    }
}

void test_id_agreement_and_failed_creation(rift::RiftContext& context)
{
    const auto first = context.create_mesh_snapshot<2>(make_triangulation<2>(), make_mapping<2>());
    boost::ut::expect(first.has_value());

    const auto failed = context.create_mesh_snapshot<2>({}, make_mapping<2>());
    boost::ut::expect(not failed.has_value());

    const auto second = context.create_mesh_snapshot<3>(make_triangulation<3>(), make_mapping<3>());
    boost::ut::expect(second.has_value());
    if (!first.has_value() || !second.has_value()) {
        return;
    }

    const auto first_ids = dealii::Utilities::MPI::all_gather(context.mpi_communicator(), (*first)->id().value());
    const auto second_ids = dealii::Utilities::MPI::all_gather(context.mpi_communicator(), (*second)->id().value());
    boost::ut::expect(std::ranges::all_of(
        first_ids, [expected = first_ids.front()](const std::uint64_t id) { return id == expected; }));
    boost::ut::expect(std::ranges::all_of(
        second_ids, [expected = second_ids.front()](const std::uint64_t id) { return id == expected; }));
    boost::ut::expect((*second)->id().value() == (*first)->id().value() + std::uint64_t{1});
    boost::ut::expect((*first)->communicator() == MPI_COMM_WORLD);
    boost::ut::expect((*second)->communicator() == MPI_COMM_WORLD);
}

} // namespace

int main(int argc, char** argv)
{
    using namespace boost::ut;

    rift::RiftContext actual_context(argc, argv);

    // Boost.UT suites cannot capture runtime state during static registration.
    static rift::RiftContext* context_ptr = nullptr;
    context_ptr = &actual_context;

    [[maybe_unused]] const suite<"MeshSnapshotAgreement"> suite = [] {
        auto& context = *context_ptr;
        "collective mesh validation reports asymmetric null resources"_test = [&context] {
            test_asymmetric_null_resources(context);
        };
        "collective mesh validation rejects non-world communicators"_test = [&context] {
            test_non_world_communicators(context);
        };
        "collective mesh validation requires dimension agreement"_test = [&context] {
            test_dimension_agreement(context);
        };
        "collective mesh publication assigns coherent non-reused IDs"_test = [&context] {
            test_id_agreement_and_failed_creation(context);
        };
    };

    const auto result = static_cast<int>(cfg<>.run());
    context_ptr = nullptr;
    return result;
}
