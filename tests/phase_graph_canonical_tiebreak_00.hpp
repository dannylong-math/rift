#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/phase_graph.hpp>
#include <vector>

namespace rift_test::phase_graph_canonical_tiebreak_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "canonical interface ordering compares every logical field"_test = [] {
        const auto phases = std::vector<rift::PhaseSpecification>{{"a", "pa"}, {"b", "pb"}, {"c", "pc"}};
        const auto callback = rift::test::accept_all_interfaces;

        const auto plus_tiebreak = rift::make_phase_graph(
            rift::test::make_test_run(), phases, {{"edge", "a", "c", "law"}, {"edge", "a", "b", "law"}}, callback);
        expect(!plus_tiebreak.has_value());
        expect(rift::test::has_error(plus_tiebreak.error(), rift::PhaseGraphErrorCode::duplicate_interface_name));

        const auto key_tiebreak = rift::make_phase_graph(
            rift::test::make_test_run(), phases, {{"edge", "a", "b", "z-law"}, {"edge", "a", "b", "a-law"}}, callback);
        expect(!key_tiebreak.has_value());
        expect(rift::test::has_error(key_tiebreak.error(), rift::PhaseGraphErrorCode::duplicate_interface_name));
    };
}

} // namespace rift_test::phase_graph_canonical_tiebreak_00
