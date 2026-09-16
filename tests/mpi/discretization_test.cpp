#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_config.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <deal.II/base/enable_observer_pointer.h>
#include <deal.II/base/exceptions.h>
#include <deal.II/base/observer_pointer.h>
#include <deal.II/base/types.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/grid/tria.h>
#include <limits>
#include <memory>
// Use the public MPI header; MPICH declares functions in an unexported nested header.
#include <mpi.h>
#include <rift/context.hpp>
#include <rift/discretization.hpp>
#include <type_traits>
#include <utility>

namespace {
// Borrow the mutable Context owned by main across Catch2 test callbacks.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
rift::Context* test_context = nullptr;

template<int dim> void check_type_contract()
{
    using Discretization = rift::Discretization<dim>;
    using Triangulation = dealii::parallel::distributed::Triangulation<dim>;
    static_assert(std::is_constructible_v<Discretization, rift::Context&>);
    static_assert(!std::is_default_constructible_v<Discretization>);
    static_assert(!std::is_copy_constructible_v<Discretization>);
    static_assert(!std::is_copy_assignable_v<Discretization>);
    static_assert(!std::is_move_constructible_v<Discretization>);
    static_assert(!std::is_move_assignable_v<Discretization>);
    static_assert(std::is_base_of_v<dealii::EnableObserverPointer, Discretization>);
    static_assert(std::is_same_v<decltype(std::declval<Discretization&>().triangulation()), Triangulation&>);
    static_assert(
        std::is_same_v<decltype(std::declval<const Discretization&>().triangulation()), const Triangulation&>);
    static_assert(std::is_same_v<decltype(std::declval<const Discretization&>().n_global_active_cells()),
                                 dealii::types::global_cell_index>);
    static_assert(std::is_same_v<decltype(std::declval<const Discretization&>().n_active_cells()), unsigned int>);
    static_assert(std::is_same_v<decltype(std::declval<const Discretization&>().n_levels()), unsigned int>);
    static_assert(noexcept(std::declval<Discretization&>().triangulation()));
    static_assert(noexcept(std::declval<const Discretization&>().triangulation()));
    static_assert(noexcept(std::declval<const Discretization&>().n_global_active_cells()));
    static_assert(noexcept(std::declval<const Discretization&>().n_active_cells()));
    static_assert(noexcept(std::declval<const Discretization&>().n_levels()));
}

// Local counts include every stored cell, including ghosts and artificial cells.
// The global oracle counts each physical cell exactly once, on its owning rank.
template<int dim>
// Catch2 assertion expansions inflate the complexity of these independent count checks.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void check_counts(const rift::Discretization<dim>& discretization, const unsigned long long expected_global_cells,
                  const unsigned int expected_global_levels)
{
    const auto& triangulation = discretization.triangulation();
    if (expected_global_cells == 0) {
        CHECK(discretization.n_global_active_cells() == 0);
        CHECK(discretization.n_active_cells() == 0);
        CHECK(discretization.n_levels() == 0);
        return;
    }
    unsigned int local_active = 0;
    unsigned long long local_owned = 0;
    for (const auto& cell : triangulation.active_cell_iterators()) {
        ++local_active;
        if (cell->is_locally_owned()) {
            ++local_owned;
        }
    }
    unsigned int local_levels = 0;
    for (const auto& cell : triangulation.cell_iterators()) {
        const auto levels = static_cast<unsigned int>(cell->level() + 1);
        local_levels = std::max(levels, local_levels);
    }
    unsigned long long global_owned = 0;
    unsigned int global_levels = 0;
    // NOLINTNEXTLINE(misc-include-cleaner): the MPI API is provided by <mpi.h>.
    REQUIRE(MPI_Allreduce(&local_owned, &global_owned, 1, MPI_UNSIGNED_LONG_LONG, MPI_SUM, test_context->mpi_comm()) ==
            MPI_SUCCESS);
    REQUIRE(MPI_Allreduce(&local_levels, &global_levels, 1, MPI_UNSIGNED, MPI_MAX, test_context->mpi_comm()) ==
            MPI_SUCCESS);
    CHECK(discretization.n_active_cells() == local_active);
    CHECK(discretization.n_levels() == local_levels);
    CHECK(discretization.n_global_active_cells() == global_owned);
    CHECK(global_owned == expected_global_cells);
    CHECK(global_levels == expected_global_levels);
}

template<int dim>
// Keep all geometry oracles together; Catch2 assertions contribute macro-only branches.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void check_cube_geometry(const rift::Discretization<dim>& discretization, const double lower, const double upper,
                         const bool colorized)
{
    double local_volume = 0;
    std::array<double, dim> local_min{};
    std::array<double, dim> local_max{};
    for (unsigned int axis = 0; std::cmp_less(axis, dim); ++axis) {
        local_min.at(axis) = std::numeric_limits<double>::infinity();
        local_max.at(axis) = -std::numeric_limits<double>::infinity();
    }
    for (const auto& cell : discretization.triangulation().active_cell_iterators()) {
        if (!cell->is_locally_owned()) {
            continue;
        }
        local_volume += cell->measure();
        for (const auto vertex : cell->vertex_indices()) {
            for (unsigned int axis = 0; std::cmp_less(axis, dim); ++axis) {
                // deal.II Point has no at(); the loop bounds guarantee a valid component.
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
                const auto coordinate = cell->vertex(vertex)[axis];
                CHECK(coordinate >= lower);
                CHECK(coordinate <= upper);
                if (coordinate < local_min.at(axis)) {
                    local_min.at(axis) = coordinate;
                }
                if (coordinate > local_max.at(axis)) {
                    local_max.at(axis) = coordinate;
                }
            }
        }
        for (const auto& face : cell->face_iterators()) {
            if (!face->at_boundary()) {
                continue;
            }
            bool found_boundary_plane = false;
            for (unsigned int axis = 0; std::cmp_less(axis, dim); ++axis) {
                // deal.II Point has no at(); axis is bounded by its dimension.
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
                const auto coordinate = face->center()[axis];
                if (coordinate == lower || coordinate == upper) {
                    found_boundary_plane = true;
                    const auto expected_id = colorized ? 2 * axis + (coordinate == upper) : 0;
                    CHECK(face->boundary_id() == expected_id);
                }
            }
            CHECK(found_boundary_plane);
        }
    }
    double global_volume = 0;
    std::array<double, dim> global_min{};
    std::array<double, dim> global_max{};
    REQUIRE(MPI_Allreduce(&local_volume, &global_volume, 1, MPI_DOUBLE, MPI_SUM, test_context->mpi_comm()) ==
            MPI_SUCCESS);
    REQUIRE(MPI_Allreduce(local_min.data(), global_min.data(), dim, MPI_DOUBLE, MPI_MIN, test_context->mpi_comm()) ==
            MPI_SUCCESS);
    REQUIRE(MPI_Allreduce(local_max.data(), global_max.data(), dim, MPI_DOUBLE, MPI_MAX, test_context->mpi_comm()) ==
            MPI_SUCCESS);
    double expected_volume = 1;
    for (unsigned int axis = 0; std::cmp_less(axis, dim); ++axis) {
        expected_volume *= upper - lower;
        CHECK(global_min.at(axis) == lower);
        CHECK(global_max.at(axis) == upper);
    }
    CHECK(global_volume == Catch::Approx(expected_volume).epsilon(1e-12));
}

// Catch2 assertions inflate complexity; keep construction and destruction in one scenario.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
template<int dim> void check_lifetime_and_empty_grid()
{
    CAPTURE(dim);
    check_type_contract<dim>();
    const auto subscriptions = test_context->n_subscriptions();
    {
        rift::Discretization<dim> discretization(*test_context);
        CHECK(test_context->n_subscriptions() == subscriptions + 1);
        CHECK(discretization.triangulation().get_mesh_smoothing() ==
              (dealii::Triangulation<dim>::smoothing_on_refinement |
               dealii::Triangulation<dim>::smoothing_on_coarsening));
        CHECK(std::addressof(discretization.triangulation()) ==
              std::addressof(std::as_const(discretization).triangulation()));
        int comparison = MPI_UNEQUAL;
        REQUIRE(MPI_Comm_compare(discretization.triangulation().get_mpi_communicator(), test_context->mpi_comm(),
                                 &comparison) == MPI_SUCCESS);
        CHECK(comparison == MPI_IDENT);
        check_counts(discretization, 0, 0);
        const auto observers = discretization.n_subscriptions();
        {
            const dealii::ObserverPointer<rift::Discretization<dim>> observer(&discretization, "discretization test");
            CHECK(observer.get() == std::addressof(discretization));
            CHECK(discretization.n_subscriptions() == observers + 1);
            CHECK(std::addressof(observer->triangulation()) == std::addressof(discretization.triangulation()));
        }
        CHECK(discretization.n_subscriptions() == observers);
        // Exercise destruction with a populated triangulation, while Context remains alive.
        discretization.generate_grid("hyper_cube", "0 : 1 : false");
    }
    CHECK(test_context->n_subscriptions() == subscriptions);
    int finalized = 1;
    // NOLINTNEXTLINE(misc-include-cleaner): the MPI API is provided by <mpi.h>.
    REQUIRE(MPI_Finalized(&finalized) == MPI_SUCCESS);
    CHECK(finalized == 0);
}

template<int dim> void check_generation_and_refinement()
{
    CAPTURE(dim);
    rift::Discretization<dim> discretization(*test_context);
    const dealii::ObserverPointer<rift::Discretization<dim>> observer(&discretization, "refinement observer");
    discretization.generate_grid("hyper_cube", "-1 : 2 : true");
    check_counts(discretization, 1, 1);
    check_cube_geometry(discretization, -1, 2, true);
    discretization.refine_global(0);
    check_counts(discretization, 1, 1);
    check_cube_geometry(discretization, -1, 2, true);
    discretization.refine_global(2);
    check_counts(discretization, 1ULL << (2 * dim), 3);
    check_cube_geometry(discretization, -1, 2, true);
    discretization.refine_global(1);
    check_counts(discretization, 1ULL << (3 * dim), 4);
    check_cube_geometry(discretization, -1, 2, true);
    CHECK(observer.get() == std::addressof(discretization));
    CHECK(observer->n_global_active_cells() == (1ULL << (3 * dim)));
}

template<int dim> void check_subdivision_and_mutable_access()
{
    CAPTURE(dim);
    rift::Discretization<dim> discretization(*test_context);
    discretization.generate_grid("subdivided_hyper_cube", "2 : -2 : 2 : false");
    check_counts(discretization, 1ULL << dim, 1);
    check_cube_geometry(discretization, -2, 2, false);
    auto& triangulation = discretization.triangulation();
    triangulation.refine_global(1);
    check_counts(discretization, 1ULL << (2 * dim), 2);
    check_cube_geometry(discretization, -2, 2, false);
    triangulation.clear();
    check_counts(discretization, 0, 0);
    discretization.generate_grid("hyper_cube", "0 : 1 : false");
    check_counts(discretization, 1, 1);
    check_cube_geometry(discretization, 0, 1, false);
}

template<int dim> void check_generator_failures()
{
    CAPTURE(dim);
    rift::Discretization<dim> discretization(*test_context);
    CHECK_THROWS_AS(discretization.generate_grid("not_a_grid_generator", ""), dealii::ExceptionBase);
    check_counts(discretization, 0, 0);
    CHECK_THROWS_AS(discretization.generate_grid("hyper_cube", "not-a-number : 2 : true"), dealii::ExceptionBase);
    check_counts(discretization, 0, 0);
    discretization.generate_grid("hyper_cube", "0 : 1 : false");
    check_counts(discretization, 1, 1);
    check_cube_geometry(discretization, 0, 1, false);
}
} // namespace

TEST_CASE("Discretization borrows Context and exposes one initially empty triangulation", "[discretization][mpi]")
{
    REQUIRE(test_context != nullptr);
    check_lifetime_and_empty_grid<2>();
    check_lifetime_and_empty_grid<3>();
}

TEST_CASE("Discretization forwards grid geometry and accumulates global refinements", "[discretization][mpi]")
{
    REQUIRE(test_context != nullptr);
    check_generation_and_refinement<2>();
    check_generation_and_refinement<3>();
}

TEST_CASE("Discretization forwards generator names and reflects direct triangulation changes", "[discretization][mpi]")
{
    REQUIRE(test_context != nullptr);
    check_subdivision_and_mutable_access<2>();
    check_subdivision_and_mutable_access<3>();
}

TEST_CASE("Discretization propagates grid selection and parsing errors and remains usable", "[discretization][mpi]")
{
    REQUIRE(test_context != nullptr);
    check_generator_failures<2>();
    check_generator_failures<3>();
}

int main(int argc, char** argv)
{
    int result = EXIT_FAILURE;
    {
        rift::Context context(argc, argv);
        test_context = &context;
        Catch::Session session;
        result = session.applyCommandLine(argc, argv);
        if (result == 0) {
            // Catch2 randomizes case order by default. All ranks must run the
            // same order so their mesh operations and collectives match.
            auto& config = session.configData();
            // NOLINTNEXTLINE(misc-include-cleaner): the MPI API is provided by <mpi.h>.
            if (MPI_Bcast(&config.rngSeed, 1, MPI_UINT32_T, 0, context.mpi_comm()) != MPI_SUCCESS) {
                result = EXIT_FAILURE;
            }
            else {
                config.rngSeedWasFixed = true;
                result = session.run();
            }
        }
        test_context = nullptr;
    }
    int finalized = 0;
    if (MPI_Finalized(&finalized) != MPI_SUCCESS || finalized == 0) {
        return EXIT_FAILURE;
    }
    return result;
}
