#include <boost/ut.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <rift/logging.hpp>
#include <rift/rift_context.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

[[nodiscard]] std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

static_assert(!std::is_copy_constructible_v<rift::Logger>);
static_assert(!std::is_move_constructible_v<rift::Logger>);
static_assert(std::is_base_of_v<std::runtime_error, rift::LoggingInitializationError>);

} // namespace

int main(int argc, char** argv)
{
    using namespace boost::ut;

    const rift::LoggingInitializationError empty_error({});
    expect(std::string_view{empty_error.what()} == "Rift logging initialization failed");

    const auto directory = std::filesystem::current_path() / "logging_test_output" / "nested";
    const auto base_path = directory / "rift.log";
    const auto rank_path = directory / "rift.rank-0.log";
    std::filesystem::remove_all(directory.parent_path());

    std::ostringstream terminal; // NOLINT(misc-const-correctness): receives writes through redirected std::cout.
    std::streambuf* original_buffer = nullptr;
    int result = 1;
    {
        rift::RiftContext actual_context(argc, argv, 1,
                                         {.terminal_enabled = true,
                                          .terminal_level = rift::LogLevel::warning,
                                          .file_base_path = base_path,
                                          .file_level = rift::LogLevel::debug,
                                          .file_mode = rift::LogFileMode::truncate});
        original_buffer = std::cout.rdbuf(terminal.rdbuf());

        actual_context.logger().trace("trace {}", 1);
        actual_context.logger().debug("debug {}", 2);
        actual_context.logger().info("info {}", 3);
        actual_context.logger().warning("warning {}", 4);
        actual_context.logger().error("error {}", 5);
        actual_context.logger().critical("critical {}", 6);
        actual_context.logger().log(rift::LogLevel::off, "off message");
        expect(throws<std::invalid_argument>([&actual_context] {
            // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): verifies the public validation boundary.
            actual_context.logger().log(static_cast<rift::LogLevel>(99), "invalid level");
        }));
        actual_context.pcout() << "pcout message\n";
        actual_context.logger().flush();
        const auto terminal_text = terminal.str();
        const auto file_text = read_file(rank_path);
        const auto& const_context = actual_context;
        const_context.logger().info("const logger access");
        const bool pcout_active = const_context.pcout().is_active();
        std::cout.rdbuf(original_buffer);

        static const std::string* terminal_text_ptr = nullptr;
        static const std::string* file_text_ptr = nullptr;
        static bool pcout_was_active = false;
        terminal_text_ptr = &terminal_text;
        file_text_ptr = &file_text;
        pcout_was_active = pcout_active;

        [[maybe_unused]] const suite<"Logging"> suite = [] {
            "independent thresholds route explicit messages and pcout only to their selected sinks"_test = [] {
                expect(terminal_text_ptr->contains("warning 4"));
                expect(terminal_text_ptr->contains("error 5"));
                expect(terminal_text_ptr->contains("critical 6"));
                expect(terminal_text_ptr->contains("pcout message"));
                expect(!terminal_text_ptr->contains("debug 2"));
                expect(!terminal_text_ptr->contains("info 3"));
                expect(!terminal_text_ptr->contains("off message"));

                expect(file_text_ptr->contains("[rift.rank-0]"));
                expect(file_text_ptr->contains("debug 2"));
                expect(file_text_ptr->contains("info 3"));
                expect(file_text_ptr->contains("warning 4"));
                expect(file_text_ptr->contains("error 5"));
                expect(file_text_ptr->contains("critical 6"));
                expect(!file_text_ptr->contains("trace 1"));
                expect(!file_text_ptr->contains("pcout message"));
                expect(!file_text_ptr->contains("off message"));
                expect(pcout_was_active);
            };
        };

        result = static_cast<int>(cfg<>.run());
        terminal_text_ptr = nullptr;
        file_text_ptr = nullptr;
    }
    std::filesystem::remove_all(directory.parent_path());
    return result;
}
