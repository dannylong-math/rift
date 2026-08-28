#include <boost/ut.hpp>
#include <rift/version.hpp>

int main()
{
    using namespace boost::ut;

    "Rift reports its configured semantic version"_test = [] {
        const auto version = rift::current_version();

        expect(version.major == 0_i);
        expect(version.minor == 1_i);
        expect(version.patch == 0_i);
    };

    return static_cast<int>(cfg<>.run());
}
