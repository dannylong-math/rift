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
#include <stdexcept>
#include <string>

namespace rift_test::mpi::phase_graph_callback_exception_00 {

struct LocalRegistryFailure {};

inline int run_test()
{
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    const auto run = rift::RunConfiguration::create(MPI_COMM_WORLD);
    if (!run) {
        return 1;
    }

    try {
        [[maybe_unused]] const auto unexpected_result = rift::make_phase_graph(
            *run, {{"gas", "compressible"}, {"liquid", "low-mach"}}, {{"surface", "liquid", "gas", "law"}},
            [rank](const auto&, const auto&, const auto&) -> std::optional<std::string> {
                if (rank == 0) {
                    throw LocalRegistryFailure{};
                }
                return std::nullopt;
            });
        std::cerr << "callback exception unexpectedly produced a graph result\n";
        return 1;
    }
    catch (const LocalRegistryFailure&) {
        if (rank != 0) {
            return 1;
        }
    }
    catch (const std::runtime_error& error) {
        if (rank == 0 ||
            std::string(error.what()) != "compatibility callback threw on another rank for interface 'surface'") {
            return 1;
        }
    }
    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "callback exceptions produce one deterministic collective phase graph error"_test = [] { expect(run_test() == 0); };
}

} // namespace rift_test::mpi::phase_graph_callback_exception_00
