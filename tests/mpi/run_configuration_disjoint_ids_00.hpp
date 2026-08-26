#pragma once

#include <array>
#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <iostream>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <rift/run_configuration.hpp>

namespace rift_test::mpi::run_configuration_disjoint_ids_00 {

inline int run_test()
{

    const auto run = rift::RunConfiguration::create(MPI_COMM_SELF);
    if (!run) {
        std::cerr << run.error().message << '\n';
        return 1;
    }

    const std::uint64_t local_id = run->id().value();
    std::array<std::uint64_t, 2> job_ids{};
    MPI_Allgather(&local_id, 1, MPI_UINT64_T, job_ids.data(), 1, MPI_UINT64_T, MPI_COMM_WORLD);
    if (job_ids.at(0) == job_ids.at(1)) {
        std::cerr << "disjoint MPI_COMM_SELF runs reused one run identity\n";
        return 1;
    }
    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "disjoint MPI self runs receive distinct run identities"_test = [] { expect(run_test() == 0); };
}

} // namespace rift_test::mpi::run_configuration_disjoint_ids_00
