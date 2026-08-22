#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <rift/phase_graph.hpp>

int main()
{
    using namespace boost::ut;

    "interface operator keys may not be empty"_test = [] {
        const auto result =
            rift::make_phase_graph({{"gas", "compressible"}, {"liquid", "low-mach"}},
                                   {{"surface", "liquid", "gas", ""}}, rift::test::accept_all_interfaces);

        expect(!result.has_value());
        expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::empty_interface_operator_key));
        expect(result.error().front().message.contains("surface"));
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
