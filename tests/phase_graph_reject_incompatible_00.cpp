#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>

int main()
{
    using namespace boost::ut;

    "a construction-time validator rejects incompatible interface models"_test = [] {
        const rift::InterfaceCompatibilityCheck reject = [](const rift::PhaseDescriptor&, const rift::PhaseDescriptor&,
                                                            const rift::InterfaceSpecification&) {
            return std::optional<std::string>{"no compiled kernel supports this phase pairing"};
        };
        const auto result = rift::make_phase_graph({{"solid", "thermomechanical"}, {"gas", "compressible"}},
                                                   {{"surface", "solid", "gas", "finite-rate"}}, reject);

        expect(!result.has_value());
        expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::incompatible_interface));
        expect(result.error().front().message.contains("surface"));
        expect(result.error().front().message.contains("solid"));
        expect(result.error().front().message.contains("gas"));
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
