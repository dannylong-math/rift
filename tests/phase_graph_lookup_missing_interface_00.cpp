#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <rift/phase_graph.hpp>

int main()
{
    using namespace boost::ut;

    "missing interfaces do not resolve by name or phase pair"_test = [] {
        const auto result =
            rift::make_phase_graph({{"gas", "compressible"}, {"liquid", "low-mach"}, {"solid", "elastic"}},
                                   {{"surface", "liquid", "gas", "finite-rate"}}, rift::test::accept_all_interfaces);

        expect(result.has_value());
        expect(!result->find_interface("absent").has_value());

        const auto gas = result->find_phase("gas").value();
        const auto solid = result->find_phase("solid").value();
        expect(!result->find_interface(gas, solid).has_value());
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
