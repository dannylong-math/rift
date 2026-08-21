#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>

int main()
{
    using namespace boost::ut;

    "phase and interface names resolve to stable numeric ids"_test = [] {
        const auto result =
            rift::make_phase_graph({{"liquid", "low-mach"}, {"gas", "compressible"}},
                                   {{"surface", "liquid", "gas", "finite-rate"}}, rift::test::accept_all_interfaces);

        expect(result.has_value());
        const auto gas = result->find_phase("gas");
        const auto surface = result->find_interface("surface");
        expect(gas.has_value());
        expect(surface.has_value());
        expect(result->phase(*gas).name == "gas");
        expect(result->material_interface(*surface).name == "surface");
        expect(!result->find_phase("missing").has_value());
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
