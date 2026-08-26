#pragma once

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <iostream>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <optional>
#include <rift/run_configuration.hpp>

namespace rift_test::mpi::run_configuration_reject_intercommunicator_00 {

inline int run_test()
{

    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    std::optional<rift::RunConfiguration> consumed_run;
    if (rank == 0) {
        consumed_run.emplace(rift::RunConfiguration::create(MPI_COMM_SELF).value());
    }

    MPI_Comm local = MPI_COMM_NULL;
    MPI_Comm_split(MPI_COMM_WORLD, rank, 0, &local);
    MPI_Comm intercommunicator = MPI_COMM_NULL;
    MPI_Intercomm_create(local, 0, MPI_COMM_WORLD, 1 - rank, 41, &intercommunicator);

    const auto result = rift::RunConfiguration::create(intercommunicator);

    MPI_Comm_free(&intercommunicator);
    MPI_Comm_free(&local);
    if (result.has_value()) {
        std::cerr << "Rift accepted an intercommunicator after local ID histories diverged\n";
        return 1;
    }
    if (result.error().code != rift::RunConfigurationErrorCode::intercommunicator_not_supported) {
        std::cerr << "Rift returned the wrong intercommunicator diagnostic\n";
        return 1;
    }
    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "run construction rejects an intercommunicator after rank histories diverge"_test = [] { expect(run_test() == 0); };
}

} // namespace rift_test::mpi::run_configuration_reject_intercommunicator_00
