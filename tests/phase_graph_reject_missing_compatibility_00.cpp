#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <rift/phase_graph.hpp>

int main()
{
    using namespace boost::ut;

    "an interface graph requires an explicit compatibility check"_test = [] {
        const auto result = rift::make_phase_graph({{"gas", "compressible"}, {"liquid", "low-mach"}},
                                                   {{"surface", "liquid", "gas", "finite-rate"}});

        expect(!result.has_value());
        expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::missing_compatibility_check));
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
