#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>

int main()
{
    using namespace boost::ut;

    "the initial graph has at most one edge per unordered phase pair"_test = [] {
        const auto result =
            rift::make_phase_graph({{"gas", "compressible"}, {"liquid", "low-mach"}},
                                   {{"first", "gas", "liquid", "law-a"}, {"second", "liquid", "gas", "law-b"}},
                                   rift::test::accept_all_interfaces);

        expect(!result.has_value());
        expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::duplicate_phase_pair));
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
