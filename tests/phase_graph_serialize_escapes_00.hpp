#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/phase_graph.hpp>
#include <string>

namespace rift_test::phase_graph_serialize_escapes_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "canonical serialization escapes every JSON control character"_test = [] {
        const auto run = rift::test::make_test_run();
        std::string phase_name = "phase\"\\\b\f\n\r\t";
        phase_name.push_back('\x01');

        const auto result = rift::make_phase_graph(run, {{phase_name, "model"}}, {});

        expect(result.has_value());
        expect(
            result->canonical_json() ==
            R"({"schema":"rift.phase_graph","version":1,"phases":[{"id":0,"name":"phase\"\\\b\f\n\r\t\u0001","physics":"model"}],"interfaces":[]})");
    };
}

} // namespace rift_test::phase_graph_serialize_escapes_00
