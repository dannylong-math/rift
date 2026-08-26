#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/phase_graph.hpp>
#include <string>

namespace rift_test::phase_graph_utf8_fields_00 {

inline bool rejects_invalid_utf8(const rift::PhaseGraphResult& result)
{
    return !result && rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::invalid_utf8);
}

} // namespace rift_test::phase_graph_utf8_fields_00

namespace rift_test::phase_graph_utf8_fields_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "every canonical graph string field requires UTF-8"_test = [] {
        const std::string bad{"\x80", 1};
        const auto callback = rift::test::accept_all_interfaces;

        expect(rejects_invalid_utf8(rift::make_phase_graph(rift::test::make_test_run(), {{bad, "physics"}}, {})));
        expect(rejects_invalid_utf8(rift::make_phase_graph(rift::test::make_test_run(), {{"phase", bad}}, {})));
        expect(rejects_invalid_utf8(rift::make_phase_graph(rift::test::make_test_run(), {{"a", "pa"}, {"b", "pb"}},
                                                           {{bad, "a", "b", "law"}}, callback)));
        expect(rejects_invalid_utf8(rift::make_phase_graph(rift::test::make_test_run(), {{"a", "pa"}, {"b", "pb"}},
                                                           {{"edge", bad, "b", "law"}}, callback)));
        expect(rejects_invalid_utf8(rift::make_phase_graph(rift::test::make_test_run(), {{"a", "pa"}, {"b", "pb"}},
                                                           {{"edge", "a", bad, "law"}}, callback)));
        expect(rejects_invalid_utf8(rift::make_phase_graph(rift::test::make_test_run(), {{"a", "pa"}, {"b", "pb"}},
                                                           {{"edge", "a", "b", bad}}, callback)));
    };
}

} // namespace rift_test::phase_graph_utf8_fields_00
