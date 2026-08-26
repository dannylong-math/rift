#pragma once

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <iostream>
#include <mpi.h>
#include <mpi_proto.h>

namespace rift_test::mpi::mpi_test_registration_00 {

inline int run_test()
{

    int rank = -1;
    int size = 0;
    const auto rank_status = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    const auto size_status = MPI_Comm_size(MPI_COMM_WORLD, &size);

    int rank_sum = 0;
    const auto reduce_status = MPI_Allreduce(&rank, &rank_sum, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    if (rank_status != MPI_SUCCESS || size_status != MPI_SUCCESS || reduce_status != MPI_SUCCESS || size != 2 ||
        rank_sum != 1) {
        std::cerr << "expected exactly two MPI ranks numbered 0 and 1, got size=" << size << ", rank sum=" << rank_sum
                  << ", and MPI statuses=" << rank_status << ',' << size_status << ',' << reduce_status << '\n';
        return 1;
    }

    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "the MPI test launcher provides exactly two communicating ranks"_test = [] { expect(run_test() == 0); };
}

} // namespace rift_test::mpi::mpi_test_registration_00
