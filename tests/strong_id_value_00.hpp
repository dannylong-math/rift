#pragma once

#include <boost/ut.hpp>
#include <rift/phase_graph.hpp>

namespace rift_test::strong_id_value_00 {

inline void register_tests()
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
}

} // namespace rift_test::strong_id_value_00
