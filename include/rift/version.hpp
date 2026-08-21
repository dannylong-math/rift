#pragma once

namespace rift {

/// A semantic version number.
///
/// Each component is non-negative. Rift uses this type where exposing a
/// structured version is more useful than exposing a formatted string.
///
/// \ingroup core
struct Version {
    /// The major version component.
    int major;

    /// The minor version component.
    int minor;

    /// The patch version component.
    int patch;
};

/// Return the version of the Rift library being used.
///
/// \ingroup core
Version current_version() noexcept;

} // namespace rift
