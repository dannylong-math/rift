#include <boost/ut.hpp>
#include <rift/phase_graph.hpp>

int main()
{
    using namespace boost::ut;

    "a graph may contain one phase and no interfaces"_test = [] {
        const auto result = rift::make_phase_graph({{"gas", "compressible"}}, {});

        expect(result.has_value());
        expect(result->phases().size() == 1_u);
        expect(result->interfaces().empty());
        expect(result->phases().front().id == rift::PhaseId::from_index(0));
        expect(result->phases().front().name == "gas");
        expect(result->phases().front().physics_key.value() == "compressible");
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
