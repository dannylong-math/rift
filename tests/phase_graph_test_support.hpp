#pragma once

#include <optional>
#include <rift/phase_graph.hpp>
#include <string>

namespace rift::test {

inline const InterfaceCompatibilityCheck accept_all_interfaces =
    [](const PhaseDescriptor&, const PhaseDescriptor&, const InterfaceSpecification&) -> std::optional<std::string> {
    return std::nullopt;
};

inline bool has_error(const PhaseGraphErrors& errors, const PhaseGraphErrorCode code)
{
    for (const auto& error : errors)
        if (error.code == code)
            return true;

    return false;
}

} // namespace rift::test
