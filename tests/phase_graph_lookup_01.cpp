#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>

int main()
{
    using namespace boost::ut;

    "an interface resolves from its unordered incident phase pair"_test = [] {
        const auto result =
            rift::make_phase_graph({{"gas", "compressible"}, {"liquid", "low-mach"}},
                                   {{"surface", "liquid", "gas", "finite-rate"}}, rift::test::accept_all_interfaces);

        expect(result.has_value());
        const auto gas = result->find_phase("gas");
        const auto liquid = result->find_phase("liquid");
        expect(gas.has_value() && liquid.has_value());
        expect(result->find_interface(*gas, *liquid).has_value());
        expect(result->find_interface(*liquid, *gas).has_value());
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
