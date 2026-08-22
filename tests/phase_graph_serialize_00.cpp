#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <rift/phase_graph.hpp>

int main()
{
    using namespace boost::ut;

    "canonical serialization is independent of configuration insertion order"_test = [] {
        const auto first =
            rift::make_phase_graph({{"liquid", "low-mach"}, {"gas", "compressible"}},
                                   {{"surface", "liquid", "gas", "finite-rate"}}, rift::test::accept_all_interfaces);
        const auto second =
            rift::make_phase_graph({{"gas", "compressible"}, {"liquid", "low-mach"}},
                                   {{"surface", "liquid", "gas", "finite-rate"}}, rift::test::accept_all_interfaces);

        expect(first.has_value() && second.has_value());
        expect(first->canonical_json() == second->canonical_json());
        expect(
            first->canonical_json() ==
            R"({"schema":"rift.phase_graph","version":1,"phases":[{"id":0,"name":"gas","physics":"compressible"},{"id":1,"name":"liquid","physics":"low-mach"}],"interfaces":[{"id":0,"name":"surface","minus_phase":1,"plus_phase":0,"operator":"finite-rate"}]})");
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
