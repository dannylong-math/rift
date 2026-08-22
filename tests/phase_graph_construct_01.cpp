#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <rift/phase_graph.hpp>

int main()
{
    using namespace boost::ut;

    "a graph may contain more than two phases"_test = [] {
        const auto result = rift::make_phase_graph(
            {{"solid", "thermomechanical"}, {"liquid", "low-mach"}, {"gas", "compressible"}},
            {{"evaporation", "liquid", "gas", "finite-rate"}, {"melting", "solid", "liquid", "stefan"}},
            rift::test::accept_all_interfaces);

        expect(result.has_value());
        expect(result->phases().size() == 3_u);
        expect(result->interfaces().size() == 2_u);
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
