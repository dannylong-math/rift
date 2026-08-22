#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <rift/phase_graph.hpp>

int main()
{
    using namespace boost::ut;

    "construction reports independent configuration errors together"_test = [] {
        const auto result =
            rift::make_phase_graph({{"gas", "compressible"}, {"gas", "low-mach"}, {"liquid", "low-mach"}},
                                   {{"missing", "unknown", "liquid", "law"}, {"self", "liquid", "liquid", "law"}},
                                   rift::test::accept_all_interfaces);

        expect(!result.has_value());
        expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::duplicate_phase_name));
        expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::missing_incident_phase));
        expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::self_interface));
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
