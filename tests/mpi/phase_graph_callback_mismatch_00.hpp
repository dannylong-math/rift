#pragma once

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <iostream>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <optional>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <string>
#include <vector>

namespace rift_test::mpi::phase_graph_callback_mismatch_00 {

inline bool is_callback_mismatch(const rift::PhaseGraphResult& result)
{
    return !result && result.error().size() == 1 &&
           result.error().front().code == rift::PhaseGraphErrorCode::collective_compatibility_mismatch &&
           result.error().front().message ==
               "compatibility callback result differs across the run communicator for interface 'surface'";
}

inline int run_test()
{
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    const auto run = rift::RunConfiguration::create(MPI_COMM_WORLD);
    if (!run) {
        return 1;
    }
    const std::vector<rift::PhaseSpecification> phases{{"gas", "compressible"}, {"liquid", "low-mach"}};
    const std::vector<rift::InterfaceSpecification> interfaces{{"surface", "liquid", "gas", "law"}};

    const auto outcome = rift::make_phase_graph(
        *run, phases, interfaces, [rank](const auto&, const auto&, const auto&) -> std::optional<std::string> {
            return rank == 0 ? std::nullopt : std::optional<std::string>{"rejected"};
        });
    const auto reason = rift::make_phase_graph(
        *run, phases, interfaces, [rank](const auto&, const auto&, const auto&) -> std::optional<std::string> {
            return rank == 0 ? std::optional<std::string>{"reason-zero"} : std::optional<std::string>{"reason-one"};
        });
    if (!is_callback_mismatch(outcome) || !is_callback_mismatch(reason)) {
        std::cerr << "rank-divergent callback outcomes did not return one deterministic mismatch\n";
        return 1;
    }
    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "rank-divergent callbacks produce one deterministic phase graph mismatch"_test = [] { expect(run_test() == 0); };
}

} // namespace rift_test::mpi::phase_graph_callback_mismatch_00
