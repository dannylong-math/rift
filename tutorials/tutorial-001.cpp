#include <cstddef>
#include <deal.II/base/mpi.h>
#include <iostream>
#include <mpi.h>
#include <optional>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

/**
 * \brief Let every rank agree whether a tutorial invariant holds.
 * \param[in] local_condition Whether the invariant holds on this rank.
 * \param[in] message Explanation printed by rank zero when any rank fails.
 * \return `true` only when the condition holds on every rank.
 */
bool collective_check(const bool local_condition, const std::string_view message)
{
    const int local_value = local_condition ? 1 : 0;
    const int global_value = dealii::Utilities::MPI::min(local_value, MPI_COMM_WORLD);
    const auto rank = dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD);
    if (global_value == 0 && rank == 0) {
        std::cerr << "Tutorial 1 failed: " << message << '\n';
    }
    return global_value != 0;
}

/**
 * \brief Print phase-graph construction errors once for the MPI job.
 * \param[in] errors The collectively agreed graph diagnostics.
 */
void print_graph_errors(const rift::PhaseGraphErrors& errors)
{
    const auto rank = dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD);
    if (rank == 0) {
        for (const auto& error : errors) {
            std::cerr << error.message << '\n';
        }
    }
}

/**
 * \brief Run the phase-graph lesson while MPI is available.
 * \return Zero after the complete workflow succeeds on every rank.
 */
int run_tutorial()
{
    // rift:snippet-begin tutorial-001.run-configuration
    auto run_result = rift::RunConfiguration::create(MPI_COMM_WORLD);
    if (!collective_check(run_result.has_value(), "Rift could not create the run configuration")) {
        if (!run_result) {
            std::cerr << run_result.error().message << '\n';
        }
        return 1;
    }
    auto run = std::move(*run_result);
    // rift:snippet-end tutorial-001.run-configuration

    // rift:snippet-begin tutorial-001.phase-and-interface-input
    const std::vector<rift::PhaseSpecification> phases{
        {"gas", "compressible"},
        {"liquid", "low-mach"},
    };
    const std::vector<rift::InterfaceSpecification> interfaces{
        {"free_surface", "liquid", "gas", "finite-rate"},
    };

    std::size_t compatibility_calls = 0;
    bool compatibility_arguments_match = true;
    const rift::InterfaceCompatibilityCheck compatibility =
        [&compatibility_calls,
         &compatibility_arguments_match](const rift::PhaseDescriptor& minus, const rift::PhaseDescriptor& plus,
                                         const rift::InterfaceSpecification& interface) -> std::optional<std::string> {
        ++compatibility_calls;
        compatibility_arguments_match =
            compatibility_arguments_match && minus.name == "liquid" && minus.physics_key.value() == "low-mach" &&
            plus.name == "gas" && plus.physics_key.value() == "compressible" && interface.name == "free_surface" &&
            interface.minus_phase == "liquid" && interface.plus_phase == "gas" &&
            interface.operator_key.value() == "finite-rate";
        const bool available = minus.physics_key.value() == "low-mach" && plus.physics_key.value() == "compressible" &&
                               interface.operator_key.value() == "finite-rate";
        return available ? std::nullopt : std::optional<std::string>{"this phase/operator pairing is unavailable"};
    };
    // rift:snippet-end tutorial-001.phase-and-interface-input

    // rift:snippet-begin tutorial-001.build-graph
    auto graph_result = rift::make_phase_graph(run, phases, interfaces, compatibility);
    if (!collective_check(graph_result.has_value(), "the phase graph input was rejected")) {
        if (!graph_result) {
            print_graph_errors(graph_result.error());
        }
        return 1;
    }
    auto graph = std::move(*graph_result);
    const bool compatibility_trace_is_expected = compatibility_calls == 1 && compatibility_arguments_match;
    if (!collective_check(compatibility_trace_is_expected,
                          "the compatibility callback did not receive the exact oriented interface once per rank")) {
        return 1;
    }
    // rift:snippet-end tutorial-001.build-graph

    // rift:snippet-begin tutorial-001.inspect-graph
    const auto liquid_id = graph.find_phase("liquid");
    const auto gas_id = graph.find_phase("gas");
    const auto interface_id = graph.find_interface("free_surface");
    if (!collective_check(liquid_id && gas_id && interface_id, "a configured graph name was not found")) {
        return 1;
    }
    if (!liquid_id || !gas_id || !interface_id) {
        return 1;
    }

    const auto liquid = graph.reference(*liquid_id);
    const auto gas = graph.reference(*gas_id);
    if (!collective_check(liquid && gas, "a phase reference could not be created")) {
        return 1;
    }

    const auto& edge = graph.material_interface(*interface_id);
    const bool graph_is_expected = graph.phases().size() == 2 && graph.interfaces().size() == 1 &&
                                   graph.owns(*liquid) && graph.owns(*gas) && edge.minus_phase == liquid->phase &&
                                   edge.plus_phase == gas->phase && edge.operator_key.value() == "finite-rate";
    if (!collective_check(graph_is_expected, "the resolved graph does not match the requested topology")) {
        return 1;
    }
    // rift:snippet-end tutorial-001.inspect-graph

    const auto rank = dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD);
    if (rank == 0) {
        std::cout << "Tutorial 1 complete: 2 phases and 1 liquid-to-gas interface.\n";
    }
    return 0;
}

} // namespace

/**
 * \brief Keep the MPI lifetime outside every object created by Tutorial 1.
 * \param[in] argc Number of command-line arguments.
 * \param[in] argv Command-line arguments forwarded to the MPI runtime.
 * \return The collective tutorial result.
 */
int main(int argc, char** argv)
{
    const dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    return run_tutorial();
}
