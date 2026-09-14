/**
 * \file
 * \brief Rank-aware synchronous logging implementation.
 */

#include <algorithm>
#include <cstdint>
#include <deal.II/base/conditional_ostream.h>
#include <deal.II/base/mpi.h>
#include <filesystem>
#include <format>
#include <iostream>
#include <iterator>
#include <memory>
#include <mpi.h>
#include <optional>
#include <rift/logging.hpp>
#include <rift/rift_context.hpp>
#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/ostream_sink.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace rift {

namespace {

/** \brief Serialization-friendly exact logging-option record. */
struct LoggingOptionsWire {
    /** \brief Whether rank zero receives terminal records. */
    bool terminal_enabled = false;
    /** \brief Encoded terminal threshold. */
    std::uint8_t terminal_level = 0;
    /** \brief Whether rank-local file sinks are requested. */
    bool file_enabled = false;
    /** \brief Portable generic spelling of the configured base path. */
    std::string file_base_path;
    /** \brief Encoded file threshold. */
    std::uint8_t file_level = 0;
    /** \brief Encoded existing-file policy. */
    std::uint8_t file_mode = 0;

    /** \brief Serialize this fixed semantic record for world agreement. */
    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & terminal_enabled;
        archive & terminal_level;
        archive & file_enabled;
        archive & file_base_path;
        archive & file_level;
        archive & file_mode;
    }

    /** \brief Compare every configured option exactly. */
    friend bool operator==(const LoggingOptionsWire&, const LoggingOptionsWire&) = default;
};

/** \brief Serialization-friendly logging initialization diagnostic. */
struct LoggingIssueWire {
    /** \brief Encoded issue classification. */
    std::uint8_t code = 0;
    /** \brief Rank that observed the issue. */
    unsigned int rank = 0;
    /** \brief Whether `path` carries a configured destination. */
    bool has_path = false;
    /** \brief Portable generic spelling of the affected path. */
    std::string path;
    /** \brief Human-readable issue detail. */
    std::string message;

    /** \brief Serialize one diagnostic for failure-only collection. */
    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & code;
        archive & rank;
        archive & has_path;
        archive & path;
        archive & message;
    }
};

/** \brief Return whether a possibly cast level is a public enumerator. */
[[nodiscard]] constexpr bool valid_level(const LogLevel level) noexcept
{
    return static_cast<std::uint8_t>(level) <= static_cast<std::uint8_t>(LogLevel::off);
}

/** \brief Return whether a possibly cast mode is a public enumerator. */
[[nodiscard]] constexpr bool valid_file_mode(const LogFileMode mode) noexcept
{
    return mode == LogFileMode::append || mode == LogFileMode::truncate;
}

/** \brief Convert Rift severity to the private spdlog backend. */
[[nodiscard]] spdlog::level::level_enum to_spdlog_level(const LogLevel level)
{
    switch (level) {
    case LogLevel::trace:
        return spdlog::level::trace;
    case LogLevel::debug:
        return spdlog::level::debug;
    case LogLevel::info:
        return spdlog::level::info;
    case LogLevel::warning:
        return spdlog::level::warn;
    case LogLevel::error:
        return spdlog::level::err;
    case LogLevel::critical:
        return spdlog::level::critical;
    case LogLevel::off:
        return spdlog::level::off;
    }
    throw std::invalid_argument("invalid Rift log level");
}

/** \brief Make an exact portable comparison record from public options. */
[[nodiscard]] LoggingOptionsWire make_wire(const LoggingOptions& options)
{
    return {.terminal_enabled = options.terminal_enabled,
            .terminal_level = static_cast<std::uint8_t>(options.terminal_level),
            .file_enabled = options.file_base_path.has_value(),
            .file_base_path = options.file_base_path.has_value() ? options.file_base_path->generic_string() : "",
            .file_level = static_cast<std::uint8_t>(options.file_level),
            .file_mode = static_cast<std::uint8_t>(options.file_mode)};
}

/** \brief Add one local typed issue. */
void add_issue(std::vector<LoggingInitializationIssue>& issues, const LoggingInitializationIssueCode code,
               const unsigned int rank, std::optional<std::filesystem::path> path, std::string message)
{
    issues.push_back({.code = code, .rank = rank, .path = std::move(path), .message = std::move(message)});
}

/** \brief Collect complete initialization diagnostics only after failure. */
[[nodiscard]] std::vector<LoggingInitializationIssue>
collect_issues(const std::vector<LoggingInitializationIssue>& local_issues)
{
    std::vector<LoggingIssueWire> local_wire;
    local_wire.reserve(local_issues.size());
    std::ranges::transform(local_issues, std::back_inserter(local_wire), [](const auto& issue) {
        return LoggingIssueWire{.code = static_cast<std::uint8_t>(issue.code),
                                .rank = issue.rank,
                                .has_path = issue.path.has_value(),
                                .path = issue.path.has_value() ? issue.path->generic_string() : "",
                                .message = issue.message};
    });

    const auto gathered = dealii::Utilities::MPI::all_gather(MPI_COMM_WORLD, local_wire);
    std::vector<LoggingInitializationIssue> issues;
    for (const auto& rank_issues : gathered) {
        std::ranges::transform(rank_issues, std::back_inserter(issues), [](const auto& issue) {
            return LoggingInitializationIssue{.code = static_cast<LoggingInitializationIssueCode>(issue.code),
                                              .rank = issue.rank,
                                              .path = issue.has_path ? std::optional{std::filesystem::path{issue.path}}
                                                                     : std::nullopt,
                                              .message = issue.message};
        });
    }
    std::ranges::sort(issues, [](const auto& left, const auto& right) {
        const auto left_path = left.path.has_value() ? left.path->generic_string() : std::string{};
        const auto right_path = right.path.has_value() ? right.path->generic_string() : std::string{};
        return std::tuple{left.rank, left.code, left_path, left.message} <
               std::tuple{right.rank, right.code, right_path, right.message};
    });
    return issues;
}

/** \brief Throw collectively when any rank recorded an issue. */
void throw_if_any_issue(const std::vector<LoggingInitializationIssue>& local_issues)
{
    const auto local_failed = static_cast<unsigned int>(!local_issues.empty());
    if (dealii::Utilities::MPI::max(local_failed, MPI_COMM_WORLD) != 0U) {
        throw LoggingInitializationError(collect_issues(local_issues));
    }
}

/** \brief Pair one world rank with the communicator size used for padding. */
struct RankCoordinates {
    /** \brief Calling process's world rank. */
    unsigned int rank;
    /** \brief Number of processes in the world communicator. */
    unsigned int world_size;
};

/** \brief Insert a naturally sorted rank suffix before the base extension. */
[[nodiscard]] std::filesystem::path rank_log_path(const std::filesystem::path& base_path,
                                                  const RankCoordinates coordinates)
{
    const auto width = std::to_string(coordinates.world_size).size();
    const auto rank_text = std::format("{:0{}}", coordinates.rank, width);
    const auto filename =
        std::format("{}.rank-{}{}", base_path.stem().string(), rank_text, base_path.extension().string());
    return base_path.parent_path() / filename;
}

/** \brief Format the stable exception summary without discarding typed issues. */
[[nodiscard]] std::string exception_message(const std::vector<LoggingInitializationIssue>& issues)
{
    if (issues.empty()) {
        return "Rift logging initialization failed";
    }
    std::string message = "Rift logging initialization failed with ";
    message += std::to_string(issues.size());
    message += issues.size() == 1U ? " issue: " : " issues: ";
    message += issues.front().message;
    return message;
}

} // namespace

/** \brief Hide the spdlog backend from Rift's public interface. */
class Logger::Impl {
public:
    /** \brief Private synchronous spdlog logger with rank-selected sinks. */
    explicit Impl(std::shared_ptr<spdlog::logger> logger) : logger_(std::move(logger)) {}

    /** \brief Backend logger not registered in spdlog global state. */
    std::shared_ptr<spdlog::logger> logger_;
};

LoggingInitializationError::LoggingInitializationError(std::vector<LoggingInitializationIssue> issues) :
    std::runtime_error(exception_message(issues)), issues_(std::move(issues))
{
}

const std::vector<LoggingInitializationIssue>& LoggingInitializationError::issues() const noexcept { return issues_; }

Logger::Logger(const LoggingOptions& options, const unsigned int rank, const unsigned int world_size)
{
    std::vector<LoggingInitializationIssue> issues;
    if (!valid_level(options.terminal_level)) {
        add_issue(issues, LoggingInitializationIssueCode::invalid_level, rank, std::nullopt,
                  std::format("rank {} supplied an invalid terminal log level", rank));
    }
    if (!valid_level(options.file_level)) {
        add_issue(issues, LoggingInitializationIssueCode::invalid_level, rank, options.file_base_path,
                  std::format("rank {} supplied an invalid file log level", rank));
    }
    if (!valid_file_mode(options.file_mode)) {
        add_issue(issues, LoggingInitializationIssueCode::invalid_file_mode, rank, options.file_base_path,
                  std::format("rank {} supplied an invalid file mode", rank));
    }
    if (options.file_base_path.has_value() &&
        (options.file_base_path->empty() || options.file_base_path->filename().empty())) {
        add_issue(issues, LoggingInitializationIssueCode::invalid_file_path, rank, options.file_base_path,
                  std::format("rank {} supplied a file base path without a filename", rank));
    }

    const auto local_options = make_wire(options);
    const auto gathered_options = dealii::Utilities::MPI::all_gather(MPI_COMM_WORLD, local_options);
    if (!std::ranges::all_of(gathered_options,
                             [&local_options](const auto& remote) { return remote == local_options; })) {
        add_issue(issues, LoggingInitializationIssueCode::collective_options_mismatch, rank, options.file_base_path,
                  std::format("rank {} supplied logging options that differ across MPI_COMM_WORLD", rank));
    }
    throw_if_any_issue(issues);

    if (rank == 0U && options.file_base_path.has_value() && !options.file_base_path->parent_path().empty()) {
        std::error_code error;
        std::filesystem::create_directories(options.file_base_path->parent_path(), error);
        if (error) {
            add_issue(issues, LoggingInitializationIssueCode::directory_creation_failed, rank,
                      options.file_base_path->parent_path(),
                      std::format("could not create logging directory '{}': {}",
                                  options.file_base_path->parent_path().string(), error.message()));
        }
    }
    throw_if_any_issue(issues);

    std::vector<spdlog::sink_ptr> sinks;
    try {
        if (rank == 0U && options.terminal_enabled) {
            auto terminal = std::make_shared<spdlog::sinks::ostream_sink_mt>(std::cout);
            terminal->set_level(to_spdlog_level(options.terminal_level));
            sinks.push_back(std::move(terminal));
        }
        if (options.file_base_path.has_value()) {
            const auto path = rank_log_path(*options.file_base_path, {.rank = rank, .world_size = world_size});
            const bool truncate = options.file_mode == LogFileMode::truncate;
            auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), truncate);
            file->set_level(to_spdlog_level(options.file_level));
            sinks.push_back(std::move(file));
        }
    }
    catch (const spdlog::spdlog_ex& error) {
        const auto path = rank_log_path(*options.file_base_path, {.rank = rank, .world_size = world_size});
        add_issue(issues, LoggingInitializationIssueCode::sink_open_failed, rank, path,
                  std::format("rank {} could not open its requested logging sink: {}", rank, error.what()));
    }
    throw_if_any_issue(issues);

    auto backend = std::make_shared<spdlog::logger>(std::format("rift.rank-{}", rank), sinks.begin(), sinks.end());
    backend->set_level(spdlog::level::trace);
    backend->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
    implementation_ = std::make_unique<Impl>(std::move(backend));
}

Logger::~Logger()
{
    // spdlog contains sink failures in its configured error handler, including
    // failures raised while this explicit final flush is performed.
    implementation_->logger_->flush();
}

void Logger::log(const LogLevel level, const std::string_view message) const
{
    if (level == LogLevel::off) {
        return;
    }
    implementation_->logger_->log(to_spdlog_level(level), spdlog::string_view_t{message.data(), message.size()});
}

void Logger::flush() const { implementation_->logger_->flush(); }

RiftContext::RiftContext(int& argc, char**& argv, const unsigned int max_threads,
                         LoggingOptions logging) : // NOLINT(performance-unnecessary-value-param): approved value API.
    mpi_lifetime_(argc, argv, max_threads)
{
    try {
        // std::make_unique cannot invoke Logger's private context-only constructor.
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        logger_.reset(new Logger(logging, dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD),
                                 dealii::Utilities::MPI::n_mpi_processes(MPI_COMM_WORLD)));
        pcout_ = std::make_unique<dealii::ConditionalOStream>(
            std::cout, logging.terminal_enabled && dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD) == 0U);
    }
    catch (const LoggingInitializationError&) {
        // Logger initialization errors are collective, so finalization cannot
        // strand ranks. deal.II intentionally skips MPI_Finalize while an
        // exception is unwinding; finalize here before rethrowing unchanged.
        mpi_lifetime_.finalize();
        throw;
    }
}

RiftContext::~RiftContext() = default;

Logger& RiftContext::logger() noexcept { return *logger_; }

const Logger& RiftContext::logger() const noexcept { return *logger_; }

const dealii::ConditionalOStream& RiftContext::pcout() const noexcept { return *pcout_; }

} // namespace rift
