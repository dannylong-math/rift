#pragma once

#include <algorithm>
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

namespace rift_test::mpi::phase_graph_input_permutation_agreement_00 {

inline int run_test()
{
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    const auto run = rift::RunConfiguration::create(MPI_COMM_WORLD);
    if (!run) {
        return 1;
    }
    std::vector<rift::PhaseSpecification> phases{{"alpha", "pa"}, {"beta", "pb"}, {"gamma", "pg"}};
    std::vector<rift::InterfaceSpecification> interfaces{
        {"ab", "alpha", "beta", "lab"}, {"bc", "beta", "gamma", "lbc"}, {"ca", "gamma", "alpha", "lca"}};
    if (rank != 0) {
        std::ranges::reverse(phases);
        std::ranges::reverse(interfaces);
    }

    std::vector<std::string> trace;
    const auto graph =
        rift::make_phase_graph(*run, phases, interfaces,
                               [&trace](const auto&, const auto&, const auto& interface) -> std::optional<std::string> {
                                   trace.push_back(interface.name);
                                   return std::nullopt;
                               });
    const std::string expected_json =
        R"({"schema":"rift.phase_graph","version":1,"phases":[{"id":0,"name":"alpha","physics":"pa"},{"id":1,"name":"beta","physics":"pb"},{"id":2,"name":"gamma","physics":"pg"}],"interfaces":[{"id":0,"name":"ab","minus_phase":0,"plus_phase":1,"operator":"lab"},{"id":1,"name":"bc","minus_phase":1,"plus_phase":2,"operator":"lbc"},{"id":2,"name":"ca","minus_phase":2,"plus_phase":0,"operator":"lca"}]})";
    if (!graph || graph->canonical_json() != expected_json || trace != std::vector<std::string>{"ab", "bc", "ca"}) {
        std::cerr << "declaration permutations did not produce one canonical graph and callback trace\n";
        return 1;
    }
    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "declaration permutations produce one canonical graph and callback trace"_test = [] { expect(run_test() == 0); };
}

} // namespace rift_test::mpi::phase_graph_input_permutation_agreement_00
