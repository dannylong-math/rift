#include <catch2/catch_test_macros.hpp>
#include <rift/version.hpp>

static_assert(noexcept(rift::current_version()));

TEST_CASE("Rift reports its configured semantic version","[version]")
{
    const auto version = rift::current_version();

    CHECK(version.major == 0);
    CHECK(version.minor == 1);
    CHECK(version.patch == 0);
}
