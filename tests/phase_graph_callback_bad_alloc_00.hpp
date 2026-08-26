#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <new>
#include <optional>
#include <rift/phase_graph.hpp>
#include <string>

namespace rift_test::phase_graph_callback_bad_alloc_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "callback bad_alloc is rethrown as the original user exception"_test = [] {
        try {
            [[maybe_unused]] const auto unexpected_result = rift::make_phase_graph(
                rift::test::make_test_run(), {{"gas", "compressible"}, {"liquid", "low-mach"}},
                {{"surface", "liquid", "gas", "law"}},
                [](const auto&, const auto&, const auto&) -> std::optional<std::string> { throw std::bad_alloc{}; });
            expect(false);
        }
        catch (const std::bad_alloc&) {
            expect(true);
        }
    };
}

} // namespace rift_test::phase_graph_callback_bad_alloc_00
