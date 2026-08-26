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
#include <string_view>
#include <vector>

namespace rift_test::mpi::phase_graph_input_mismatch_00 {

inline bool is_input_mismatch(const rift::PhaseGraphResult& result)
{
    return !result && result.error().size() == 1 &&
           result.error().front().code == rift::PhaseGraphErrorCode::collective_input_mismatch &&
           result.error().front().message == "phase graph input differs across the run communicator";
}

inline int run_test()
{
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    const auto run = rift::RunConfiguration::create(MPI_COMM_WORLD);
    if (!run) {
        return 1;
    }
    const auto callback = [](const auto&, const auto&, const auto&) -> std::optional<std::string> {
        return std::nullopt;
    };

    const std::vector<rift::PhaseSpecification> phases{{"gas", "compressible"}, {"liquid", "low-mach"}};
    const std::vector<rift::InterfaceSpecification> interfaces{{"surface", "liquid", "gas", "law"}};
    std::vector<rift::PhaseGraphResult> results;

    auto changed_phases = phases;
    if (rank != 0) {
        changed_phases.front().name = "vapor";
    }
    results.push_back(rift::make_phase_graph(*run, changed_phases, interfaces, callback));

    changed_phases = phases;
    if (rank != 0) {
        changed_phases.front().physics_key = rift::PhysicsKey{"other-physics"};
    }
    results.push_back(rift::make_phase_graph(*run, changed_phases, interfaces, callback));

    auto changed_interfaces = interfaces;
    if (rank != 0) {
        changed_interfaces.front().name = "other-surface";
    }
    results.push_back(rift::make_phase_graph(*run, phases, changed_interfaces, callback));

    changed_interfaces = interfaces;
    if (rank != 0) {
        std::swap(changed_interfaces.front().minus_phase, changed_interfaces.front().plus_phase);
    }
    results.push_back(rift::make_phase_graph(*run, phases, changed_interfaces, callback));

    changed_interfaces = interfaces;
    if (rank != 0) {
        changed_interfaces.front().operator_key = rift::InterfaceOperatorKey{"other-law"};
    }
    results.push_back(rift::make_phase_graph(*run, phases, changed_interfaces, callback));

    changed_interfaces = interfaces;
    if (rank != 0) {
        changed_interfaces.emplace_back("extra", "gas", "liquid", "other-law");
    }
    results.push_back(rift::make_phase_graph(*run, phases, changed_interfaces, callback));

    const rift::InterfaceCompatibilityCheck available = rank == 0 ? callback : rift::InterfaceCompatibilityCheck{};
    results.push_back(rift::make_phase_graph(*run, phases, interfaces, available));

    for (const auto& result : results) {
        if (!is_input_mismatch(result)) {
            std::cerr << "rank-divergent logical input did not return one deterministic mismatch\n";
            return 1;
        }
    }
    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "rank-divergent logical inputs produce one deterministic phase graph mismatch"_test = [] {
        expect(run_test() == 0);
    };
}

} // namespace rift_test::mpi::phase_graph_input_mismatch_00
