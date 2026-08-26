#include "test_runner_support.hpp"
#include "test_version.hpp"

#include <boost/ut.hpp>

int main(int argc, char** argv)
{
    const auto mode = rift_test::test_mode(argc, argv);
    if (mode != "serial") {
        return rift_test::invalid_test_mode("version_run_tests", mode);
    }
    [[maybe_unused]] boost::ut::suite<"Version"> const subject_suite = [] {
        rift_test::test_version::register_tests();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
