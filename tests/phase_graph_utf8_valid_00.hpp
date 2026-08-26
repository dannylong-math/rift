#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/phase_graph.hpp>
#include <string>
#include <vector>

namespace rift_test::phase_graph_utf8_valid_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "valid multibyte spellings remain byte-exact and unnormalized"_test = [] {
        const std::string composed = "\xc3\xa9";
        const std::string decomposed = "e\xcc\x81";
        const std::string nul_name{"nul\0phase", 9};
        const auto run = rift::test::make_test_run();
        const auto result = rift::make_phase_graph(
            run, {{composed, "fysikk-\xf0\x9f\x8c\x8a"}, {decomposed, "physics"}, {nul_name, "control"}}, {});

        expect(result.has_value());
        expect(result->find_phase(composed).has_value());
        expect(result->find_phase(decomposed).has_value());
        expect(result->find_phase(nul_name).has_value());
        expect(result->find_phase(composed) != result->find_phase(decomposed));
        expect(result->canonical_json().contains("nul\\u0000phase"));
        expect(result->canonical_json().contains("\xf0\x9f\x8c\x8a"));
    };

    "RFC 3629 boundary scalar encodings are accepted"_test = [] {
        const std::vector<std::string> encodings{
            std::string{"\x7f", 1},
            std::string{"\xc2\x80", 2},
            std::string{"\xdf\xbf", 2},
            std::string{"\xe0\xa0\x80", 3},
            std::string{"\xe1\x80\x80", 3},
            std::string{"\xed\x9f\xbf", 3},
            std::string{"\xee\x80\x80", 3},
            std::string{"\xef\xbf\xbf", 3},
            std::string{"\xf0\x90\x80\x80", 4},
            std::string{"\xf1\x80\x80\x80", 4},
            std::string{"\xf2\x80\x80\x80", 4},
            std::string{"\xf3\x80\x80\x80", 4},
            std::string{"\xf4\x8f\xbf\xbf", 4},
        };
        for (const auto& encoding : encodings) {
            const auto graph = rift::make_phase_graph(rift::test::make_test_run(), {{encoding, "physics"}}, {});
            expect(graph.has_value());
            expect(graph->find_phase(encoding).has_value());
        }
    };
}

} // namespace rift_test::phase_graph_utf8_valid_00
