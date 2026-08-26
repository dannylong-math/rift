#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <optional>
#include <rift/phase_graph.hpp>
#include <string>
#include <vector>

namespace rift_test::phase_graph_compatibility_order_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "compatibility callbacks receive oriented arguments in canonical interface order"_test = [] {
        const auto run = rift::test::make_test_run();
        std::vector<std::string> trace;
        const auto result = rift::make_phase_graph(
            run, {{"gamma", "physics-g"}, {"alpha", "physics-a"}, {"beta", "physics-b"}},
            {{"zeta", "gamma", "alpha", "law-z"},
             {"alpha-edge", "beta", "gamma", "law-a"},
             {"middle", "alpha", "beta", "law-m"}},
            [&trace](const rift::PhaseDescriptor& minus, const rift::PhaseDescriptor& plus,
                     const rift::InterfaceSpecification& interface) -> std::optional<std::string> {
                trace.push_back(interface.name + ":" + minus.name + ">" + plus.name + ":" +
                                std::string(interface.operator_key.value()));
                return std::nullopt;
            });

        expect(result.has_value());
        expect(trace == std::vector<std::string>{"alpha-edge:beta>gamma:law-a", "middle:alpha>beta:law-m",
                                                 "zeta:gamma>alpha:law-z"});
    };
}

} // namespace rift_test::phase_graph_compatibility_order_00
