#pragma once

/**
 * \file
 * \brief Semantic version information for the linked Rift library.
 */

namespace rift {

/**
 * \defgroup core Core utilities
 * \brief Small library-wide values and services shared by Rift components.
 */

/**
 * \brief Inspect the semantic version compiled into the linked Rift library.
 *
 * \par When to use
 * Read this value when writing diagnostics, recording run metadata, or checking
 * compatibility with a caller that expects a particular Rift release.
 *
 * \par Typical use
 * \code{.cpp}
 * const rift::Version version = rift::current_version();
 * std::cout << "Rift "
 *           << version.major << '.'
 *           << version.minor << '.'
 *           << version.patch << '\n';
 *
 * if (version.major != 0)
 *     std::cerr << "unsupported Rift major version" << '\n';
 * \endcode
 *
 * \par Important behavior
 * Every component is non-negative. The type intentionally stores structured
 * integers rather than prescribing a particular display or serialization
 * format.
 * \ingroup core
 */
struct Version {
    /**
     * \brief Major version component.
     */
    int major;

    /**
     * \brief Minor version component.
     */
    int minor;

    /**
     * \brief Patch version component.
     */
    int patch;
};

/**
 * \brief Obtain the structured version of the linked Rift library.
 *
 * Call this function at runtime rather than duplicating version constants in a
 * client:
 *
 * \code{.cpp}
 * const rift::Version linked_version = rift::current_version();
 * const bool supports_initial_api = linked_version.major == 0;
 * \endcode
 *
 * The function does not allocate and cannot fail.
 *
 * \return semantic version compiled into the linked library.
 * \ingroup core
 */
Version current_version() noexcept;

} // namespace rift
