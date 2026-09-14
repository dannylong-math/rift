#include <algorithm>
#include <boost/ut.hpp>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <rift/logging.hpp>
#include <rift/rift_context.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace {

[[nodiscard]] unsigned int launcher_rank()
{
    for (const auto* name : {"OMPI_COMM_WORLD_RANK", "PMI_RANK", "PMIX_RANK"}) {
        if (const auto* value = std::getenv(name); value != nullptr) {
            return static_cast<unsigned int>(std::stoul(value));
        }
    }
    return 0U;
}

[[nodiscard]] unsigned int launcher_size()
{
    for (const auto* name : {"OMPI_COMM_WORLD_SIZE", "PMI_SIZE", "PMIX_SIZE"}) {
        if (const auto* value = std::getenv(name); value != nullptr) {
            return static_cast<unsigned int>(std::stoul(value));
        }
    }
    return 1U;
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] int test_mismatch(int argc, char** argv, const std::string_view field)
{
    rift::LoggingOptions options{.terminal_enabled = true,
                                 .terminal_level = rift::LogLevel::info,
                                 .file_base_path = std::nullopt,
                                 .file_level = rift::LogLevel::debug,
                                 .file_mode = rift::LogFileMode::append};
    const bool alternate = launcher_rank() != 0U;
    if (field == "terminal_enabled") {
        options.terminal_enabled = !alternate;
    }
    else if (field == "terminal_level") {
        options.terminal_level = alternate ? rift::LogLevel::warning : rift::LogLevel::info;
    }
    else if (field == "file_enabled") {
        options.file_base_path = alternate ? std::optional{std::filesystem::path{"rift.log"}} : std::nullopt;
    }
    else if (field == "file_base_path") {
        options.file_base_path = alternate ? std::filesystem::path{"alternate.log"} : std::filesystem::path{"rift.log"};
    }
    else if (field == "file_level") {
        options.file_base_path = std::filesystem::path{"rift.log"};
        options.file_level = alternate ? rift::LogLevel::error : rift::LogLevel::debug;
    }
    else if (field == "file_mode") {
        options.file_base_path = std::filesystem::path{"rift.log"};
        options.file_mode = alternate ? rift::LogFileMode::truncate : rift::LogFileMode::append;
    }
    try {
        const rift::RiftContext context(argc, argv, 1, std::move(options));
    }
    catch (const rift::LoggingInitializationError& error) {
        boost::ut::expect(!error.issues().empty());
        boost::ut::expect(error.issues().size() == launcher_size());
        boost::ut::expect(std::ranges::all_of(error.issues(), [](const auto& issue) {
            return issue.code == rift::LoggingInitializationIssueCode::collective_options_mismatch;
        }));
        for (std::size_t index = 0; index < error.issues().size(); ++index) {
            boost::ut::expect(error.issues().at(index).rank == index);
        }
        return 0;
    }
    return 1;
}

[[nodiscard]] int test_directory_failure(int argc, char** argv)
{
    try {
        const rift::RiftContext context(argc, argv, 1,
                                        {.terminal_enabled = false,
                                         .file_base_path = "/dev/null/rift.log",
                                         .file_mode = rift::LogFileMode::append});
    }
    catch (const rift::LoggingInitializationError& error) {
        boost::ut::expect(error.issues().size() == std::size_t{1});
        boost::ut::expect(error.issues().front().code ==
                          rift::LoggingInitializationIssueCode::directory_creation_failed);
        boost::ut::expect(error.issues().front().rank == 0U);
        boost::ut::expect(error.issues().front().path == std::filesystem::path{"/dev/null"});
        return 0;
    }
    return 1;
}

[[nodiscard]] int test_invalid_options(int argc, char** argv)
{
    try {
        const rift::RiftContext context(
            argc, argv, 1,
            {.terminal_enabled = false,
             // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): validates malformed input.
             .terminal_level = static_cast<rift::LogLevel>(99),
             .file_base_path = std::filesystem::path{"invalid-directory/"},
             // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): validates malformed input.
             .file_level = static_cast<rift::LogLevel>(98),
             // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): validates malformed input.
             .file_mode = static_cast<rift::LogFileMode>(97)});
    }
    catch (const rift::LoggingInitializationError& error) {
        boost::ut::expect(error.issues().size() == std::size_t{4});
        return 0;
    }
    return 1;
}

[[nodiscard]] int test_sink_failure(int argc, char** argv)
{
    const auto directory = std::filesystem::current_path() / "logging_sink_failure";
    const auto base_path = directory / "rift.log";
    const auto rank_path = directory / "rift.rank-0.log";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(rank_path);
    try {
        const rift::RiftContext context(
            argc, argv, 1,
            {.terminal_enabled = false, .file_base_path = base_path, .file_mode = rift::LogFileMode::append});
    }
    catch (const rift::LoggingInitializationError& error) {
        boost::ut::expect(error.issues().size() == std::size_t{1});
        boost::ut::expect(error.issues().front().code == rift::LoggingInitializationIssueCode::sink_open_failed);
        boost::ut::expect(error.issues().front().path == rank_path);
        std::filesystem::remove_all(directory);
        return 0;
    }
    std::filesystem::remove_all(directory);
    return 1;
}

[[nodiscard]] int test_flush_failure(int argc, char** argv)
{
    const auto directory = std::filesystem::current_path() / "logging_flush_failure";
    const auto base_path = directory / "rift.log";
    const auto rank_path = directory / "rift.rank-0.log";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    std::filesystem::create_symlink("/dev/full", rank_path);
    bool flush_returned = false;
    {
        const rift::RiftContext context(
            argc, argv, 1,
            {.terminal_enabled = false, .file_base_path = base_path, .file_mode = rift::LogFileMode::append});
        context.logger().info("buffered output whose flush is expected to fail");
        context.logger().flush();
        flush_returned = true;
        // Context destruction retries the failed flush. spdlog contains both
        // failures in its configured error handler.
    }
    std::filesystem::remove_all(directory);
    return flush_returned ? 0 : 1;
}

[[nodiscard]] int test_off_levels(int argc, char** argv)
{
    const rift::RiftContext context(argc, argv, 1,
                                    {.terminal_enabled = true,
                                     .terminal_level = rift::LogLevel::off,
                                     .file_base_path = std::nullopt,
                                     .file_level = rift::LogLevel::off,
                                     .file_mode = rift::LogFileMode::append});
    context.logger().info("suppressed");
    context.logger().flush();
    return 0;
}

[[nodiscard]] int test_empty_path(int argc, char** argv)
{
    try {
        const rift::RiftContext context(argc, argv, 1,
                                        {.terminal_enabled = false,
                                         .file_base_path = std::filesystem::path{},
                                         .file_mode = rift::LogFileMode::append});
    }
    catch (const rift::LoggingInitializationError& error) {
        boost::ut::expect(error.issues().size() == std::size_t{1});
        boost::ut::expect(error.issues().front().code == rift::LoggingInitializationIssueCode::invalid_file_path);
        return 0;
    }
    return 1;
}

[[nodiscard]] int test_relative_file(int argc, char** argv)
{
    const auto base_path = std::filesystem::path{"logging-relative.log"};
    const auto rank_path = std::filesystem::path{"logging-relative.rank-0.log"};
    std::filesystem::remove(rank_path);
    {
        std::ofstream existing(rank_path);
        existing << "content that truncate mode must replace\n";
    }
    {
        const rift::RiftContext context(
            argc, argv, 1,
            {.terminal_enabled = false, .file_base_path = base_path, .file_mode = rift::LogFileMode::truncate});
        context.logger().info("relative path");
    }
    const auto contents = read_file(rank_path);
    std::filesystem::remove(rank_path);
    return contents.contains("relative path") && !contents.contains("content that truncate mode must replace") ? 0 : 1;
}

[[nodiscard]] int test_append_file(int argc, char** argv)
{
    const auto base_path = std::filesystem::path{"logging-append.log"};
    const auto rank_path = std::filesystem::path{"logging-append.rank-0.log"};
    std::filesystem::remove(rank_path);
    {
        std::ofstream existing(rank_path);
        existing << "preserved content\n";
    }
    bool pcout_disabled = false;
    {
        const rift::RiftContext context(
            argc, argv, 1,
            {.terminal_enabled = false, .file_base_path = base_path, .file_mode = rift::LogFileMode::append});
        context.logger().info("appended content");
        pcout_disabled = !context.pcout().is_active();
    }
    const auto contents = read_file(rank_path);
    std::filesystem::remove(rank_path);
    return pcout_disabled && contents.contains("preserved content") && contents.contains("appended content") ? 0 : 1;
}

[[nodiscard]] int test_rank_routing(int argc, char** argv)
{
    const auto base_path = std::filesystem::current_path() / "logging_agreement_output" / "rift.log";
    std::ostringstream terminal; // NOLINT(misc-const-correctness): receives writes through redirected std::cout.
    auto* const original_buffer = std::cout.rdbuf(terminal.rdbuf());
    int result = 1;
    {
        rift::RiftContext context(argc, argv, 1,
                                  {.terminal_enabled = true,
                                   .terminal_level = rift::LogLevel::info,
                                   .file_base_path = base_path,
                                   .file_level = rift::LogLevel::trace,
                                   .file_mode = rift::LogFileMode::truncate});
        const auto rank = context.this_mpi_process();
        const auto width = std::to_string(context.n_mpi_processes()).size();
        const auto rank_path = base_path.parent_path() / std::format("rift.rank-{:0{}}.log", rank, width);
        context.logger().trace("private trace from rank {}", rank);
        context.logger().info("visible info from rank {}", rank);
        context.pcout() << "pcout from rank " << rank << '\n';
        context.logger().flush();

        const auto file = read_file(rank_path);
        boost::ut::expect(file.contains(std::format("private trace from rank {}", rank)));
        boost::ut::expect(file.contains(std::format("visible info from rank {}", rank)));
        boost::ut::expect(file.contains(std::format("[rift.rank-{}]", rank)));
        if (rank == 0U) {
            boost::ut::expect(terminal.str().contains("visible info from rank 0"));
            boost::ut::expect(terminal.str().contains("pcout from rank 0"));
        }
        else {
            boost::ut::expect(terminal.str().empty());
            boost::ut::expect(!context.pcout().is_active());
        }
        result = static_cast<int>(boost::ut::cfg<>.run());
    }
    std::cout.rdbuf(original_buffer);
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): conventional argc-guarded CLI access.
    const auto mode = argc > 1 ? std::string_view{argv[1]} : std::string_view{"routing"};
    if (mode.starts_with("mismatch_")) {
        return test_mismatch(argc, argv, mode.substr(std::string_view{"mismatch_"}.size()));
    }
    if (mode == "directory_failure") {
        return test_directory_failure(argc, argv);
    }
    if (mode == "invalid_options") {
        return test_invalid_options(argc, argv);
    }
    if (mode == "sink_failure") {
        return test_sink_failure(argc, argv);
    }
    if (mode == "flush_failure") {
        return test_flush_failure(argc, argv);
    }
    if (mode == "off_levels") {
        return test_off_levels(argc, argv);
    }
    if (mode == "empty_path") {
        return test_empty_path(argc, argv);
    }
    if (mode == "relative_file") {
        return test_relative_file(argc, argv);
    }
    if (mode == "append_file") {
        return test_append_file(argc, argv);
    }
    return test_rank_routing(argc, argv);
}
