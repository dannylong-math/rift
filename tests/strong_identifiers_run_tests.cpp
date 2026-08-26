#include "strong_id_value_00.hpp"
#include "test_runner_support.hpp"

#include <boost/ut.hpp>

int main(int argc, char** argv)
{
    const auto mode = rift_test::test_mode(argc, argv);
    if (mode != "serial") {
        return rift_test::invalid_test_mode("strong_identifiers_run_tests", mode);
    }
    [[maybe_unused]] boost::ut::suite<"Strong identifiers"> const subject_suite = [] {
        rift_test::strong_id_value_00::register_tests();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
