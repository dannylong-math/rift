#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <limits>
#include <rift/phase_graph.hpp>
#include <stdexcept>

namespace rift_test::phase_graph_invalid_numeric_lookup_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "maximum numeric identifiers cannot alias valid descriptors"_test = [] {
        const auto graph = rift::make_phase_graph(rift::test::make_test_run(), {{"phase", "physics"}}, {}).value();

        expect(throws<std::out_of_range>([&graph] {
            static_cast<void>(graph.phase(rift::PhaseId::from_index(std::numeric_limits<std::uint32_t>::max())));
        }));
        expect(throws<std::out_of_range>([&graph] {
            static_cast<void>(
                graph.material_interface(rift::InterfaceId::from_index(std::numeric_limits<std::uint32_t>::max())));
        }));
    };
}

} // namespace rift_test::phase_graph_invalid_numeric_lookup_00
