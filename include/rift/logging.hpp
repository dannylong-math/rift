#pragma once

/**
 * \file
 * \brief Explicit rank-aware logging configuration and facade.
 */

#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rift {

class RiftContext;

/** \brief Severity threshold understood by Rift logging sinks. */
enum class LogLevel : std::uint8_t {
    /** \brief Finest diagnostic detail. */
    trace,
    /** \brief Developer-oriented diagnostic detail. */
    debug,
    /** \brief Ordinary progress information. */
    info,
    /** \brief Recoverable condition requiring attention. */
    warning,
    /** \brief Operation failure. */
    error,
    /** \brief Process-threatening failure. */
    critical,
    /** \brief Disable a sink. */
    off,
};

/** \brief Select whether a configured rank-local file is preserved or replaced. */
enum class LogFileMode : std::uint8_t {
    /** \brief Add new records after existing file contents. */
    append,
    /** \brief Replace the rank-local file during logger construction. */
    truncate,
};

/** \brief Configure rank-zero terminal and optional per-rank file logging. */
struct LoggingOptions {
    /** \brief Whether world rank zero receives a terminal sink and active `pcout`. */
    bool terminal_enabled = true;
    /** \brief Minimum terminal severity. */
    LogLevel terminal_level = LogLevel::info;
    /** \brief Optional unsuffixed base path used to derive one path per rank. */
    std::optional<std::filesystem::path> file_base_path;
    /** \brief Minimum file severity. */
    LogLevel file_level = LogLevel::debug;
    /** \brief Existing-file behavior for each rank-local sink. */
    LogFileMode file_mode = LogFileMode::append;
};

/** \brief Classify one recoverable logger-construction defect. */
enum class LoggingInitializationIssueCode : std::uint8_t {
    /** \brief A level enumerator is outside the public domain. */
    invalid_level,
    /** \brief A file-mode enumerator is outside the public domain. */
    invalid_file_mode,
    /** \brief A configured base path has no filename. */
    invalid_file_path,
    /** \brief Ranks supplied different logging options. */
    collective_options_mismatch,
    /** \brief World rank zero could not create the requested parent directory. */
    directory_creation_failed,
    /** \brief One rank could not construct a requested sink. */
    sink_open_failed,
};

/** \brief Describe one rank-local logging initialization issue. */
struct LoggingInitializationIssue {
    /** \brief Machine-readable issue category. */
    LoggingInitializationIssueCode code;
    /** \brief World rank associated with the issue. */
    unsigned int rank;
    /** \brief Affected path, when the issue concerns a file destination. */
    std::optional<std::filesystem::path> path;
    /** \brief Immediately usable human-readable diagnostic. */
    std::string message;
};

/** \brief Report identical ordered logger-construction issues on every rank. */
class LoggingInitializationError : public std::runtime_error {
public:
    /**
     * \brief Construct an exception from the complete collective issue list.
     * \param issues ordered issues observed across `MPI_COMM_WORLD`.
     */
    explicit LoggingInitializationError(std::vector<LoggingInitializationIssue> issues);

    /** \brief Return the complete ordered issue list. */
    [[nodiscard]] const std::vector<LoggingInitializationIssue>& issues() const noexcept;

private:
    /** \brief Stable structured diagnostics retained by the exception. */
    std::vector<LoggingInitializationIssue> issues_;
};

/**
 * \brief Synchronous explicit logging facade owned by one `RiftContext`.
 *
 * The facade does not replace spdlog's global logger. Scientific functions do
 * not use it implicitly; applications decide which diagnostics to submit.
 */
class Logger {
public:
    /** \brief Flush and release every configured sink. */
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    /** \brief Submit one already formatted message at `level`. */
    void log(LogLevel level, std::string_view message) const;

    /** \brief Format and submit a trace message. */
    template<class... Args> void trace(std::format_string<Args...> format, Args&&... args) const
    {
        log(LogLevel::trace, std::format(format, std::forward<Args>(args)...));
    }

    /** \brief Format and submit a debug message. */
    template<class... Args> void debug(std::format_string<Args...> format, Args&&... args) const
    {
        log(LogLevel::debug, std::format(format, std::forward<Args>(args)...));
    }

    /** \brief Format and submit an informational message. */
    template<class... Args> void info(std::format_string<Args...> format, Args&&... args) const
    {
        log(LogLevel::info, std::format(format, std::forward<Args>(args)...));
    }

    /** \brief Format and submit a warning message. */
    template<class... Args> void warning(std::format_string<Args...> format, Args&&... args) const
    {
        log(LogLevel::warning, std::format(format, std::forward<Args>(args)...));
    }

    /** \brief Format and submit an error message. */
    template<class... Args> void error(std::format_string<Args...> format, Args&&... args) const
    {
        log(LogLevel::error, std::format(format, std::forward<Args>(args)...));
    }

    /** \brief Format and submit a critical message. */
    template<class... Args> void critical(std::format_string<Args...> format, Args&&... args) const
    {
        log(LogLevel::critical, std::format(format, std::forward<Args>(args)...));
    }

    /** \brief Flush every configured synchronous sink. */
    void flush() const;

private:
    friend class RiftContext;

    /** \brief Collectively create rank-selected sinks from agreed options. */
    Logger(const LoggingOptions& options, unsigned int rank, unsigned int world_size);

    class Impl;
    /** \brief Hide spdlog and its sink types from Rift's public headers. */
    std::unique_ptr<Impl> implementation_;
};

} // namespace rift
