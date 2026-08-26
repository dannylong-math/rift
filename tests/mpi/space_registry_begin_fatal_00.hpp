#pragma once

#include "../../src/run_configuration_internal.hpp"
#include "coverage_profile_flush.hpp"

#include <atomic>
#include <boost/ut.hpp>
#include <cstdint>
#include <cstdio>
#include <deal.II/base/mpi.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <memory>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <new>
#include <optional>
#include <print>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <utility>
#include <vector>

namespace rift_test::mpi::space_registry_begin_fatal_00 {

inline std::atomic_bool& target_stage_armed()
{
    static std::atomic_bool armed{false};
    return armed;
}

inline void fail_on_rank_one()
{
    if (dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD) == 1U) {
        target_stage_armed().store(true, std::memory_order_relaxed);
        throw std::bad_alloc{};
    }
}

inline int collective_maximum(const std::uint64_t local, std::uint64_t& chosen, const MPI_Comm communicator)
{
    return MPI_Allreduce(&local, &chosen, 1, MPI_UINT64_T, MPI_MAX, communicator);
}

inline int marked_abort(const MPI_Comm communicator, const int status)
{
    rift_test::mpi::flush_coverage_profile();
    if (target_stage_armed().load(std::memory_order_relaxed)) {
        std::println(stderr, "RIFT_FATAL_ORACLE operation=begin status={}", status);
    }
    else {
        std::println(stderr, "RIFT_FATAL_SETUP operation=begin status={}", status);
    }
    std::fflush(stderr);
    return MPI_Abort(communicator, status);
}

inline rift::RunConfiguration make_run()
{
    static rift::detail::RunSequenceAllocator allocator;
    return rift::detail::create_run_configuration(MPI_COMM_WORLD,
                                                  {.test_intercommunicator = MPI_Comm_test_inter,
                                                   .communicator_rank = MPI_Comm_rank,
                                                   .communicator_size = MPI_Comm_size,
                                                   .allocate_rank_buffer = rift::detail::allocate_rank_buffer,
                                                   .communicator_group = MPI_Comm_group,
                                                   .translate_ranks = MPI_Group_translate_ranks,
                                                   .free_group = MPI_Group_free,
                                                   .collective_maximum = collective_maximum,
                                                   .duplicate = MPI_Comm_dup,
                                                   .broadcast = MPI_Bcast,
                                                   .free_communicator = MPI_Comm_free,
                                                   .allocate_control = rift::detail::allocate_run_control,
                                                   .abort = marked_abort},
                                                  allocator)
        .value();
}

inline int trigger_fatal_operation()
{
    auto run = make_run();
    auto graph = rift::make_phase_graph(run, {{"gas", "compressible"}}, {}).value();
    const auto gas_id = graph.find_phase("gas");
    if (!gas_id) {
        return 2;
    }
    const auto gas_result = graph.reference(*gas_id);
    if (!gas_result) {
        return 3;
    }
    const auto gas = *gas_result;
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<2>>(MPI_COMM_WORLD);
    dealii::GridGenerator::hyper_cube(*triangulation);
    rift::SupportEnvelope requested;
    for (const auto& cell : triangulation->active_cell_iterators()) {
        if (cell->is_locally_owned()) {
            requested.insert(cell->id());
        }
    }
    std::unique_ptr<dealii::Triangulation<2>> consumed = std::move(triangulation);
    auto mesh = rift::make_mesh_snapshot(run, std::move(consumed)).value();
    rift::SpaceRegistry<2> const registry(mesh);
    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1}};
    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = gas, .mesh = mesh->id(), .locally_owned_requested_cells = std::move(requested)}};
    [[maybe_unused]] const auto result = rift::detail::SpaceRegistryAccess<2>::begin_draft(
        registry, graph, std::move(specification), std::move(supports), rift::SpaceEpoch::from_index(31), std::nullopt,
        fail_on_rank_one);
    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "space registry distributed begin invokes MPI Abort after a remote failure"_test = [] {
        const auto returned = trigger_fatal_operation();
        static_cast<void>(MPI_Barrier(MPI_COMM_WORLD));
        expect(false) << "expected MPI Abort; trigger returned " << returned;
    };
}

} // namespace rift_test::mpi::space_registry_begin_fatal_00
