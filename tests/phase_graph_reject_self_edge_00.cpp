#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>

int main()
{
    using namespace boost::ut;

    "an interface may not join a phase to itself"_test = [] {
        const auto result = rift::make_phase_graph(
            {{"gas", "compressible"}}, {{"invalid", "gas", "gas", "finite-rate"}}, rift::test::accept_all_interfaces);

        expect(!result.has_value());
        expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::self_interface));
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
