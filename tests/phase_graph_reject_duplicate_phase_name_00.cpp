#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <rift/phase_graph.hpp>

int main()
{
    using namespace boost::ut;

    "duplicate phase names are rejected"_test = [] {
        const auto result = rift::make_phase_graph({{"gas", "compressible"}, {"gas", "low-mach"}}, {});

        expect(!result.has_value());
        expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::duplicate_phase_name));
        expect(result.error().front().message.contains("gas"));
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
