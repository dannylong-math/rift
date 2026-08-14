#include <rift/version.hpp>

namespace rift {

Version current_version() noexcept {
    return {.major = 0, .minor = 1, .patch = 0};
}

} // namespace rift
