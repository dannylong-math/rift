#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>

int main()
{
    using namespace boost::ut;

    "duplicate interface names are rejected"_test = [] {
        const auto result = rift::make_phase_graph({{"a", "fluid"}, {"b", "fluid"}, {"c", "solid"}},
                                                   {{"contact", "a", "b", "law"}, {"contact", "b", "c", "law"}},
                                                   rift::test::accept_all_interfaces);

        expect(!result.has_value());
        expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::duplicate_interface_name));
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
