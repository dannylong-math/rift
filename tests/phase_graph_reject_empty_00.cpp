#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>

int main()
{
    using namespace boost::ut;

    "a runtime graph must contain at least one phase"_test = [] {
        const auto result = rift::make_phase_graph({}, {});

        expect(!result.has_value());
        expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::no_phases));
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
