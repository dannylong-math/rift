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

namespace rift_test::mpi::phase_graph_three_rank_agreement_00 {

inline bool has_only_error(const rift::PhaseGraphResult& result, const rift::PhaseGraphErrorCode code)
{
    return !result && result.error().size() == 1 && result.error().front().code == code;
}

inline int run_test()
{
    int rank = 0;
    int size = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    if (size != 3) {
        std::cerr << "three-rank agreement test requires exactly three ranks\n";
        return 1;
    }

    const auto run = rift::RunConfiguration::create(MPI_COMM_WORLD);
    if (!run) {
        return 1;
    }
    const std::vector<rift::InterfaceSpecification> interfaces{{"surface", "liquid", "gas", "law"}};
    const auto accept = [](const auto&, const auto&, const auto&) -> std::optional<std::string> {
        return std::nullopt;
    };

    const std::vector<rift::PhaseSpecification> divergent_phases{{rank == 1 ? "vapor" : "gas", "compressible"},
                                                                 {"liquid", "low-mach"}};
    const auto input_mismatch = rift::make_phase_graph(*run, divergent_phases, interfaces, accept);
    if (!has_only_error(input_mismatch, rift::PhaseGraphErrorCode::collective_input_mismatch)) {
        std::cerr << "middle-rank input divergence was not detected collectively\n";
        return 1;
    }

    const std::vector<rift::PhaseSpecification> phases{{"gas", "compressible"}, {"liquid", "low-mach"}};
    const auto callback_mismatch = rift::make_phase_graph(
        *run, phases, interfaces, [rank](const auto&, const auto&, const auto&) -> std::optional<std::string> {
            return rank == 1 ? std::optional<std::string>{"middle-rank rejection"} : std::nullopt;
        });
    if (!has_only_error(callback_mismatch, rift::PhaseGraphErrorCode::collective_compatibility_mismatch)) {
        std::cerr << "middle-rank callback divergence was not detected collectively\n";
        return 1;
    }

    const auto graph = rift::make_phase_graph(*run, phases, interfaces, accept);
    if (!graph || graph->find_interface("surface") != rift::InterfaceId::from_index(0)) {
        std::cerr << "three-rank exact agreement did not construct the canonical graph\n";
        return 1;
    }

    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "middle-rank phase graph divergence is detected and exact agreement succeeds"_test = [] {
        expect(run_test() == 0);
    };
}

} // namespace rift_test::mpi::phase_graph_three_rank_agreement_00
