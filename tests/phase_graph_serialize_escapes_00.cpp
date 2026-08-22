#include <boost/ut.hpp>
#include <rift/phase_graph.hpp>
#include <string>

int main()
{
    using namespace boost::ut;

    "canonical serialization escapes every JSON control character"_test = [] {
        std::string phase_name = "phase\"\\\b\f\n\r\t";
        phase_name.push_back('\x01');

        const auto result = rift::make_phase_graph({{phase_name, "model"}}, {});

        expect(result.has_value());
        expect(
            result->canonical_json() ==
            R"({"schema":"rift.phase_graph","version":1,"phases":[{"id":0,"name":"phase\"\\\b\f\n\r\t\u0001","physics":"model"}],"interfaces":[]})");
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
