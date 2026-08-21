#include <boost/ut.hpp>
#include <rift/phase_graph.hpp>

int main()
{
    using namespace boost::ut;
    using rift::PhaseId;

    "strong ids compare only with the same semantic id type"_test = [] {
        const auto first = PhaseId::from_index(1);
        const auto same = PhaseId::from_index(1);
        const auto later = PhaseId::from_index(2);

        expect(first == same);
        expect(first != later);
        expect(first < later);
        expect(first.value() == 1_u);
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
