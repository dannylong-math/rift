#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <optional>
#include <rift/phase_graph.hpp>
#include <string>

namespace rift_test::phase_graph_compatibility_exception_00 {

struct RegistryFailure {};

} // namespace rift_test::phase_graph_compatibility_exception_00

namespace rift_test::phase_graph_compatibility_exception_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "a local compatibility exception is rethrown unchanged"_test = [] {
        const auto run = rift::test::make_test_run();
        try {
            [[maybe_unused]] const auto unexpected_result = rift::make_phase_graph(
                run, {{"gas", "compressible"}, {"liquid", "low-mach"}}, {{"surface", "liquid", "gas", "law"}},
                [](const auto&, const auto&, const auto&) -> std::optional<std::string> { throw RegistryFailure{}; });
            expect(false);
        }
        catch (const RegistryFailure&) {
            expect(true);
        }
    };
}

} // namespace rift_test::phase_graph_compatibility_exception_00
