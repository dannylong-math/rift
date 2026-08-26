#pragma once

#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <iostream>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>

namespace rift_test::mpi::phase_graph_instance_id_agreement_00 {

inline int run_test()
{

    auto run = rift::RunConfiguration::create(MPI_COMM_WORLD);
    if (!run) {
        std::cerr << run.error().message << '\n';
        return 1;
    }
    auto first = rift::make_phase_graph(*run, {{"gas", "compressible"}}, {});
    auto second = rift::make_phase_graph(*run, {{"gas", "compressible"}}, {});
    if (!first || !second) {
        std::cerr << "phase graph construction failed\n";
        return 1;
    }

    const auto first_id = first->provenance().graph.value();
    const auto second_id = second->provenance().graph.value();
    std::uint64_t first_min = 0;
    std::uint64_t first_max = 0;
    std::uint64_t second_min = 0;
    std::uint64_t second_max = 0;
    MPI_Allreduce(&first_id, &first_min, 1, MPI_UINT64_T, MPI_MIN, MPI_COMM_WORLD);
    MPI_Allreduce(&first_id, &first_max, 1, MPI_UINT64_T, MPI_MAX, MPI_COMM_WORLD);
    MPI_Allreduce(&second_id, &second_min, 1, MPI_UINT64_T, MPI_MIN, MPI_COMM_WORLD);
    MPI_Allreduce(&second_id, &second_max, 1, MPI_UINT64_T, MPI_MAX, MPI_COMM_WORLD);

    if (first_min != first_max || second_min != second_max || first_id == second_id) {
        std::cerr << "graph IDs must agree across ranks and never be reused\n";
        return 1;
    }
    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "phase graph instance identities agree across ranks and are never reused"_test = [] { expect(run_test() == 0); };
}

} // namespace rift_test::mpi::phase_graph_instance_id_agreement_00
