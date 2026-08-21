#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>

int main()
{
    using namespace boost::ut;

    "canonical serialization separates multiple interfaces"_test = [] {
        const auto result = rift::make_phase_graph(
            {{"gas", "compressible"}, {"liquid", "low-mach"}, {"solid", "elastic"}},
            {{"evaporation", "liquid", "gas", "finite-rate"}, {"melting", "solid", "liquid", "stefan"}},
            rift::test::accept_all_interfaces);

        expect(result.has_value());
        expect(result->canonical_json().contains(R"("operator":"finite-rate"},{"id":1,"name":"melting")"));
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
