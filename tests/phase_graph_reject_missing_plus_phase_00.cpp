#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>

int main()
{
    using namespace boost::ut;

    "an interface reports a missing plus phase"_test = [] {
        const auto result =
            rift::make_phase_graph({{"gas", "compressible"}}, {{"surface", "gas", "liquid", "finite-rate"}},
                                   rift::test::accept_all_interfaces);

        expect(!result.has_value());
        expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::missing_incident_phase));
        expect(result.error().front().message.contains("plus phase 'liquid'"));
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
