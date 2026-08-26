#pragma once

#include "../src/mesh_snapshot_internal.hpp"
#include "../src/run_configuration_internal.hpp"

#include <cstdint>
#include <memory>
#include <mpi.h>
#include <rift/run_configuration.hpp>

namespace mesh_snapshot_test {

/** \brief Observable replacement for the production non-returning MPI abort handler. */
struct FatalMpiFailure {
    /** \brief Status routed to the fatal handler. */
    int status;
};

/** \brief Throw an observable sentinel from an injected fatal MPI path. */
inline int throw_abort([[maybe_unused]] MPI_Comm communicator, const int status) { throw FatalMpiFailure{status}; }

/** \brief Copy a local integer as the run-wide minimum on a one-rank test communicator. */
inline int copy_minimum(const int local, int& agreed, [[maybe_unused]] MPI_Comm communicator)
{
    agreed = local;
    return MPI_SUCCESS;
}

/** \brief Leave a one-rank sequence unchanged. */
inline int copy_sequence([[maybe_unused]] std::uint32_t& sequence, [[maybe_unused]] MPI_Comm communicator)
{
    return MPI_SUCCESS;
}

/** \brief Create complete real operations with one-rank collective wrappers. */
inline rift::detail::MeshSnapshotMpiOperations operations()
{
    return {.initialized = MPI_Initialized,
            .finalized = MPI_Finalized,
            .collective_minimum = copy_minimum,
            .test_intercommunicator = MPI_Comm_test_inter,
            .communicator_size = MPI_Comm_size,
            .allocate_rank_buffer = rift::detail::allocate_rank_buffer,
            .communicator_group = MPI_Comm_group,
            .translate_ranks = MPI_Group_translate_ranks,
            .free_group = MPI_Group_free,
            .compare = MPI_Comm_compare,
            .communicator_rank = MPI_Comm_rank,
            .broadcast_sequence = copy_sequence};
}

/** \brief Adopt a one-rank run retaining the throwing fatal handler. */
inline rift::RunConfiguration make_run(const std::uint64_t id = 0)
{
    MPI_Comm duplicate = MPI_COMM_NULL;
    if (MPI_Comm_dup(MPI_COMM_SELF, &duplicate) != MPI_SUCCESS) {
        throw FatalMpiFailure{MPI_ERR_OTHER};
    }
    auto control = std::make_shared<const rift::detail::RunConfigurationControl>(
        duplicate, rift::RunConfigurationId::from_index(id), 0, throw_abort);
    return rift::detail::RunConfigurationAccess::adopt(std::move(control));
}

} // namespace mesh_snapshot_test
