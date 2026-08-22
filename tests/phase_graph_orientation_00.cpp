#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <rift/phase_graph.hpp>

int main()
{
    using namespace boost::ut;

    "edge orientation does not depend on phase insertion order"_test = [] {
        const auto first =
            rift::make_phase_graph({{"liquid", "low-mach"}, {"gas", "compressible"}},
                                   {{"surface", "liquid", "gas", "finite-rate"}}, rift::test::accept_all_interfaces);
        const auto second =
            rift::make_phase_graph({{"gas", "compressible"}, {"liquid", "low-mach"}},
                                   {{"surface", "liquid", "gas", "finite-rate"}}, rift::test::accept_all_interfaces);

        expect(first.has_value() && second.has_value());
        const auto& first_edge = first->interfaces().front();
        const auto& second_edge = second->interfaces().front();
        expect(first->phase(first_edge.minus_phase).name == "liquid");
        expect(first->phase(first_edge.plus_phase).name == "gas");
        expect(first_edge.minus_phase == second_edge.minus_phase);
        expect(first_edge.plus_phase == second_edge.plus_phase);
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
