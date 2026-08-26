#pragma once

#include "../../src/run_configuration_internal.hpp"
#include "../state_result_test_support.hpp"

#include <atomic>
#include <cstdint>
#include <deal.II/distributed/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <memory>
#include <mpi.h>
#include <new>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rift::test {

inline std::atomic_bool& state_target_stage_armed()
{
    static std::atomic_bool armed{false};
    return armed;
}

inline void fail_state_stage_on_rank_one()
{
    int rank = 0;
    static_cast<void>(MPI_Comm_rank(MPI_COMM_WORLD, &rank));
    if (rank == 1) {
        state_target_stage_armed().store(true, std::memory_order_relaxed);
        throw std::bad_alloc{};
    }
}

inline void fail_state_dependency_on_rank_one()
{
    int rank = 0;
    static_cast<void>(MPI_Comm_rank(MPI_COMM_WORLD, &rank));
    if (rank == 1) {
        state_target_stage_armed().store(true, std::memory_order_relaxed);
        throw std::runtime_error("injected state dependency failure");
    }
}

inline int state_collective_maximum(const std::uint64_t local, std::uint64_t& chosen, const MPI_Comm communicator)
{
    return MPI_Allreduce(&local, &chosen, 1, MPI_UINT64_T, MPI_MAX, communicator);
}

inline RunConfiguration make_state_fatal_run(const detail::MpiAbort abort)
{
    static detail::RunSequenceAllocator allocator;
    auto result = detail::create_run_configuration(MPI_COMM_WORLD,
                                                   {.test_intercommunicator = MPI_Comm_test_inter,
                                                    .communicator_rank = MPI_Comm_rank,
                                                    .communicator_size = MPI_Comm_size,
                                                    .allocate_rank_buffer = detail::allocate_rank_buffer,
                                                    .communicator_group = MPI_Comm_group,
                                                    .translate_ranks = MPI_Group_translate_ranks,
                                                    .free_group = MPI_Group_free,
                                                    .collective_maximum = state_collective_maximum,
                                                    .duplicate = MPI_Comm_dup,
                                                    .broadcast = MPI_Bcast,
                                                    .free_communicator = MPI_Comm_free,
                                                    .allocate_control = detail::allocate_run_control,
                                                    .abort = abort},
                                                   allocator);
    return require_state_result(std::move(result));
}

template<int dim>
SpaceSnapshot<dim> make_state_fatal_space(const RunConfiguration& run,
                                          std::vector<RegionalEntrySpecification> regional_specifications = {})
{
    auto graph = require_state_result(make_phase_graph(run, {{"gas", "compressible"}}, {}));
    const auto gas = require_state_result(graph.reference(require_state_result(graph.find_phase("gas"))));
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    dealii::GridGenerator::hyper_cube(*triangulation);
    SupportEnvelope owned;
    for (const auto& cell : triangulation->active_cell_iterators()) {
        if (cell->is_locally_owned()) {
            owned.insert(cell->id());
        }
    }
    std::unique_ptr<dealii::Triangulation<dim>> consumed = std::move(triangulation);
    auto mesh = require_state_result(make_mesh_snapshot(run, std::move(consumed)));
    SpaceRegistry<dim> const registry(mesh);
    SpaceSpecification specification{
        .phase_fields = {{.phase = gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    std::vector<PhaseSupportSpecification> supports{
        {.phase = gas, .mesh = mesh->id(), .locally_owned_requested_cells = std::move(owned)}};
    auto draft = require_state_result(registry.begin_draft(graph, std::move(specification), std::move(supports)));
    return require_state_result(registry.finalize(draft, std::move(regional_specifications)));
}

} // namespace rift::test
