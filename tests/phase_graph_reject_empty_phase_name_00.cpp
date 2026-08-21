#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>

int main()
{
    using namespace boost::ut;

    "phase names may not be empty"_test = [] {
        const auto result = rift::make_phase_graph({{"gas", "compressible"}, {"", "low-mach"}}, {});

        expect(!result.has_value());
        expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::empty_phase_name));
        expect(result.error().front().message.contains("configuration index 1"));
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
