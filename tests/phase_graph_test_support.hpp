#pragma once

#include <algorithm>
#include <deal.II/base/mpi.h>
#include <optional>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <string>

namespace rift::test {

inline RunConfiguration make_test_run() { return RunConfiguration::create(MPI_COMM_SELF).value(); }

inline const InterfaceCompatibilityCheck accept_all_interfaces =
    [](const PhaseDescriptor&, const PhaseDescriptor&, const InterfaceSpecification&) -> std::optional<std::string> {
    return std::nullopt;
};

inline bool has_error(const PhaseGraphErrors& errors, const PhaseGraphErrorCode code)
{
    return std::ranges::any_of(errors, [code](const auto& error) { return error.code == code; });
}

} // namespace rift::test
