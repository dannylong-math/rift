#pragma once

#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <iostream>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <optional>
#include <rift/run_configuration.hpp>

namespace rift_test::mpi::run_configuration_id_agreement_00 {

inline int run_test()
{

    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    std::optional<rift::RunConfigurationId> local_id_on_root;
    if (rank == 0) {
        const auto local_run = rift::RunConfiguration::create(MPI_COMM_SELF);
        if (!local_run) {
            std::cerr << local_run.error().message << '\n';
            return 1;
        }
        local_id_on_root = local_run->id();
    }

    MPI_Comm reversed = MPI_COMM_NULL;
    MPI_Comm_split(MPI_COMM_WORLD, 0, 1 - rank, &reversed);
    const auto reversed_run = rift::RunConfiguration::create(reversed);
    MPI_Comm_free(&reversed);
    if (!reversed_run) {
        std::cerr << reversed_run.error().message << '\n';
        return 1;
    }

    const auto run = rift::RunConfiguration::create(MPI_COMM_WORLD);
    if (!run) {
        std::cerr << run.error().message << '\n';
        return 1;
    }

    const auto local_id = run->id().value();
    std::uint64_t minimum = 0;
    std::uint64_t maximum = 0;
    MPI_Allreduce(&local_id, &minimum, 1, MPI_UINT64_T, MPI_MIN, MPI_COMM_WORLD);
    MPI_Allreduce(&local_id, &maximum, 1, MPI_UINT64_T, MPI_MAX, MPI_COMM_WORLD);

    if (minimum != maximum || reversed_run->id().value() == local_id ||
        (local_id_on_root && local_id_on_root->value() == local_id)) {
        std::cerr << "run IDs disagree across ranks: min=" << minimum << ", max=" << maximum << '\n';
        return 1;
    }
    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "run identities agree across overlapping communicators and are never reused"_test = [] { expect(run_test() == 0); };
}

} // namespace rift_test::mpi::run_configuration_id_agreement_00
