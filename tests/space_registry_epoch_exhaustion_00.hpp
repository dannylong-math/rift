#pragma once

#include "../src/run_configuration_internal.hpp"
#include "discrete_state_test_support.hpp"

#include <atomic>
#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <expected>
#include <limits>
#include <memory>
#include <mpi.h>
#include <mpi_proto.h>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/run_configuration.hpp>
#include <vector>

namespace rift_test::space_registry_epoch_exhaustion_00 {

struct FatalStatus {
    int value;
};

inline int throw_fatal([[maybe_unused]] MPI_Comm communicator, const int status) { throw FatalStatus{status}; }

template<int dim> auto schema(const rift::test::SinglePhaseSpaceFixture<dim>& fixture)
{
    return rift::SpaceSpecification{
        .phase_fields = {{.phase = fixture.gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1}};
}

template<int dim> auto supports(const rift::test::SinglePhaseSpaceFixture<dim>& fixture)
{
    return std::vector<rift::PhaseSupportSpecification>{
        {.phase = fixture.gas, .mesh = fixture.mesh->id(), .locally_owned_requested_cells = fixture.cells}};
}

} // namespace rift_test::space_registry_epoch_exhaustion_00

namespace rift_test::space_registry_epoch_exhaustion_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "the maximum finite epoch sequence is an exhaustion sentinel"_test = [] {
        auto fixture = rift::test::make_single_phase_space_fixture<2>();
        std::atomic<std::uint64_t> exhausted{std::numeric_limits<std::uint32_t>::max()};
        const auto result =
            rift::detail::reserve_space_identity(rift::detail::MeshSnapshotAccess<2>::run_control(*fixture.mesh),
                                                 fixture.mesh->communicator(), fixture.mesh->id(), exhausted);
        expect(!result.has_value());
        expect(result.error().code == rift::SpaceBuildErrorCode::space_id_exhausted);

        const auto epoch = rift::detail::reserve_space_epoch_with_counter(
            rift::detail::MeshSnapshotAccess<2>::run_control(*fixture.mesh), fixture.mesh->communicator(),
            fixture.mesh->id(), exhausted);
        expect(!epoch.has_value());

        MPI_Comm owned = MPI_COMM_NULL;
        expect(MPI_Comm_dup(MPI_COMM_SELF, &owned) == MPI_SUCCESS);
        auto control = std::make_shared<const rift::detail::RunConfigurationControl>(
            owned, rift::RunConfigurationId::from_index(0), 0, throw_fatal);
        bool registry_fatal = false;
        try {
            static_cast<void>(rift::detail::reserve_space_registry_id_with_counter(control, MPI_COMM_SELF,
                                                                                   fixture.mesh->id(), exhausted));
        }
        catch (const FatalStatus& failure) {
            registry_fatal = true;
            expect(failure.value == MPI_ERR_OTHER);
        }
        expect(registry_fatal);

        rift::SpaceRegistry<2> const registry(fixture.mesh);
        const auto rejected = rift::detail::SpaceRegistryAccess<2>::begin_draft(
            registry, fixture.graph, schema(fixture), supports(fixture), std::unexpected(epoch.error()));
        expect(!rejected.has_value());
        expect(rejected.error().front().code == rift::SpaceBuildErrorCode::space_id_exhausted);

        const auto nonconverged = rift::detail::SpaceRegistryAccess<2>::begin_draft(
            registry, fixture.graph, schema(fixture), supports(fixture), rift::SpaceEpoch::from_index(1), 0);
        expect(!nonconverged.has_value());
        expect(rift::test::has_space_error(nonconverged.error(), rift::SpaceBuildErrorCode::closure_nonconvergence));
    };
}

} // namespace rift_test::space_registry_epoch_exhaustion_00
