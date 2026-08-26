#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/phase_graph.hpp>
#include <string>
#include <vector>

namespace rift_test::phase_graph_utf8_invalid_sequences_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "RFC 3629 rejects every malformed UTF-8 sequence class"_test = [] {
        const std::vector<std::string> malformed{
            std::string{"\x80", 1},
            std::string{"\xc0\x80", 2},
            std::string{"\xc1\x80", 2},
            std::string{"\xc2", 1},
            std::string{"\xc2\x41", 2},
            std::string{"\xc2\xc0", 2},
            std::string{"\xe0\x9f\x80", 3},
            std::string{"\xe1\x80\x41", 3},
            std::string{"\xed\xa0\x80", 3},
            std::string{"\xf0\x8f\xbf\xbf", 4},
            std::string{"\xf1", 1},
            std::string{"\xf1\x80", 2},
            std::string{"\xf1\x80\x80", 3},
            std::string{"\xf1\x80\x41\x80", 4},
            std::string{"\xf1\x80\x80\x41", 4},
            std::string{"\xf4\x90\x80\x80", 4},
            std::string{"\xf5\x80\x80\x80", 4},
            std::string{"\xe2\x82", 2},
        };

        for (const auto& bytes : malformed) {
            const auto run = rift::test::make_test_run();
            const auto result = rift::make_phase_graph(run, {{bytes, "physics"}}, {});
            expect(!result.has_value());
            expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::invalid_utf8));
        }
    };
}

} // namespace rift_test::phase_graph_utf8_invalid_sequences_00
