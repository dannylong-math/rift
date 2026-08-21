/**
 * \file
 * \brief Implementation of library-wide Rift metadata services.
 */

#include <rift/version.hpp>

namespace rift {

Version current_version() noexcept { return {.major = 0, .minor = 1, .patch = 0}; }

} // namespace rift
