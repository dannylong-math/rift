#pragma once

#include "../src/run_configuration_internal.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <memory>
#include <mpi.h>
#include <mpi_proto.h>
#include <new>
#include <optional>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rift_test::space_registry_operation_failures_00 {

struct FatalStatus {
    int value;
};

inline int throw_fatal([[maybe_unused]] MPI_Comm communicator, const int status) { throw FatalStatus{status}; }
inline void fail_allocation() { throw std::bad_alloc{}; }
inline void fail_dependency() { throw std::runtime_error("injected post-agreement failure"); }

inline rift::SpaceSpecification specification(const rift::PhaseReference phase)
{
    return {.phase_fields = {{.phase = phase, .name = "flow", .components = 1, .polynomial_degree = 1}},
            .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1}};
}

} // namespace rift_test::space_registry_operation_failures_00

namespace rift_test::space_registry_operation_failures_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "actual draft and finalize post-agreement failures use retained fatal policy"_test = [] {
        MPI_Comm owned = MPI_COMM_NULL;
        expect(MPI_Comm_dup(MPI_COMM_SELF, &owned) == MPI_SUCCESS);
        auto control = std::make_shared<const rift::detail::RunConfigurationControl>(
            owned, rift::RunConfigurationId::from_index(44), 0, throw_fatal);
        auto run = rift::detail::RunConfigurationAccess::adopt(control);
        auto graph = rift::make_phase_graph(run, {{"gas", "compressible"}}, {}).value();
        const auto gas_id = graph.find_phase("gas");
        if (!gas_id.has_value()) {
            throw std::logic_error("the operation-failure fixture did not contain gas");
        }
        const auto gas = graph.reference(*gas_id).value();
        auto triangulation = std::make_unique<dealii::Triangulation<2>>();
        dealii::GridGenerator::hyper_cube(*triangulation);
        const auto cell = triangulation->begin_active()->id();
        auto mesh = rift::make_mesh_snapshot(run, std::move(triangulation)).value();
        rift::SpaceRegistry<2> const registry(mesh);
        const auto supports = [&] {
            return std::vector<rift::PhaseSupportSpecification>{
                {.phase = gas, .mesh = mesh->id(), .locally_owned_requested_cells = {cell}}};
        };

        bool begin_fatal = false;
        try {
            [[maybe_unused]] const auto result = rift::detail::SpaceRegistryAccess<2>::begin_draft(
                registry, graph, specification(gas), supports(), rift::SpaceEpoch::from_index(19), std::nullopt,
                fail_allocation);
        }
        catch (const FatalStatus& failure) {
            begin_fatal = true;
            expect(failure.value == MPI_ERR_NO_MEM);
        }
        expect(begin_fatal);

        auto draft = registry.begin_draft(graph, specification(gas), supports());
        expect(draft.has_value());
        bool finalize_fatal = false;
        try {
            [[maybe_unused]] const auto result =
                rift::detail::SpaceRegistryAccess<2>::finalize(registry, *draft, {}, fail_dependency);
        }
        catch (const FatalStatus& failure) {
            finalize_fatal = true;
            expect(failure.value == MPI_ERR_OTHER);
        }
        expect(finalize_fatal);
        expect(draft->active());
        const auto recovered = registry.finalize(*draft, {});
        expect(recovered.has_value());
        expect(!draft->active());
    };
}

} // namespace rift_test::space_registry_operation_failures_00
