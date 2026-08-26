#pragma once

#include <boost/ut.hpp>
#include <rift/discrete_state.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace rift_test::space_registry_detail_framing_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "space payload framing preserves arbitrary strings and rejects malformed bytes"_test = [] {
        const std::vector<std::string> values{"", "a:b", std::string("x\0y", 3)};
        expect(rift::detail::unpack_space_strings(rift::detail::pack_space_strings(values)) == values);

        expect(throws<std::logic_error>([] { static_cast<void>(rift::detail::unpack_space_strings("3")); }));
        expect(throws<std::logic_error>([] { static_cast<void>(rift::detail::unpack_space_strings("4:abc")); }));
    };

    "artificial-cell guards never evaluate cell accessors"_test = [] {
        unsigned int visits = 0;
        auto record_visit = [&] { ++visits; };
        rift::detail::visit_non_artificial_cell(false, record_visit);
        rift::detail::visit_non_artificial_cell(true, record_visit);
        expect(visits == 1_u);
    };
}

} // namespace rift_test::space_registry_detail_framing_00
