#pragma once

#include <boost/ut.hpp>
#include <rift/version.hpp>

namespace rift_test::test_version {

inline void register_tests()
{
    using namespace boost::ut;

    "Rift reports its configured semantic version"_test = [] {
        const auto version = rift::current_version();

        expect(version.major == 0_i);
        expect(version.minor == 1_i);
        expect(version.patch == 0_i);
    };
}

} // namespace rift_test::test_version
