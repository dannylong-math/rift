#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/run_configuration.hpp>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace rift_test::space_registry_epoch_provenance_00 {

template<int dim>
rift::SpaceDraftResult<dim> make_draft(const rift::SpaceRegistry<dim>& registry,
                                       const rift::test::SinglePhaseSpaceFixture<dim>& fixture, const std::string& name)
{
    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = fixture.gas, .name = name, .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = fixture.gas, .mesh = fixture.mesh->id(), .locally_owned_requested_cells = fixture.cells}};
    return registry.begin_draft(fixture.graph, std::move(specification), std::move(supports));
}

template<int dim> void check_epoch_and_provenance()
{
    using namespace boost::ut;

    auto fixture = rift::test::make_single_phase_space_fixture<dim>();
    rift::SpaceRegistry<dim> const first_registry(fixture.mesh);
    rift::SpaceRegistry<dim> const second_registry(fixture.mesh);

    auto first = make_draft(first_registry, fixture, "flow");
    expect(first.has_value());
    expect(first->active());
    const auto first_provenance = first->provenance();
    expect(first_provenance.run == fixture.run.id());
    expect(first_provenance.graph == fixture.graph.provenance().graph);
    expect(first_provenance.mesh == fixture.mesh->id());
    expect(first->field_spaces().front().provenance() == first_provenance);
    expect(first->level_set_space().provenance() == first_provenance);
    auto changed = first_provenance;
    changed.run = rift::RunConfigurationId::from_index(changed.run.value() + 1);
    expect(changed != first_provenance);
    changed = first_provenance;
    changed.graph = rift::PhaseGraphInstanceId::from_index(changed.graph.value() + 1);
    expect(changed != first_provenance);
    changed = first_provenance;
    changed.mesh = rift::MeshSnapshotId::from_index(changed.mesh.value() + 1);
    expect(changed != first_provenance);
    changed = first_provenance;
    changed.registry = rift::SpaceRegistryId::from_index(changed.registry.value() + 1);
    expect(changed != first_provenance);
    changed = first_provenance;
    changed.epoch = rift::SpaceEpoch::from_index(changed.epoch.value() + 1);
    expect(changed != first_provenance);

    auto rejected = make_draft(first_registry, fixture, "");
    expect(!rejected.has_value());
    auto third = make_draft(first_registry, fixture, "flow");
    expect(third.has_value());
    expect(third->epoch().value() == first->epoch().value() + std::uint64_t{2});

    auto& first_draft = first.value();
    auto foreign = second_registry.finalize(first_draft, {});
    expect(!foreign.has_value());
    expect(rift::test::has_space_error(foreign.error(), rift::SpaceBuildErrorCode::foreign_registry_draft));
    expect(first_draft.active());

    auto recovered = first_registry.finalize(first_draft, {});
    expect(recovered.has_value());
    expect(!first_draft.active());
    const auto repeated = first_registry.finalize(first_draft, {});
    expect(!repeated.has_value());
    expect(rift::test::has_space_error(repeated.error(), rift::SpaceBuildErrorCode::inactive_draft));

    auto& third_draft = third.value();
    auto snapshot = first_registry.finalize(third_draft, {});
    expect(snapshot.has_value());
    expect(!third_draft.active());
    expect(snapshot->provenance() == snapshot->layout().provenance());
    expect(snapshot->field_spaces().front().provenance() == snapshot->provenance());

    auto wrong_mesh_draft = make_draft(first_registry, fixture, "wrong_mesh").value();
    auto wrong_mesh_provenance = wrong_mesh_draft.provenance();
    wrong_mesh_provenance.mesh = rift::MeshSnapshotId::from_index(wrong_mesh_provenance.mesh.value() + 1);
    wrong_mesh_draft =
        rift::detail::SpaceRegistryAccess<dim>::with_provenance(std::move(wrong_mesh_draft), wrong_mesh_provenance);
    const auto wrong_mesh_finalize = first_registry.finalize(wrong_mesh_draft, {});
    expect(rift::test::has_space_error(wrong_mesh_finalize.error(), rift::SpaceBuildErrorCode::foreign_registry_draft));
    expect(wrong_mesh_draft.active());

    auto movable = make_draft(first_registry, fixture, "movable");
    auto* const movable_draft = &movable.value();
    rift::SpaceDraft<dim> moved(std::move(*movable_draft));
    expect(!movable_draft->active());
    expect(moved.active());
    const auto moved_snapshot = first_registry.finalize(moved, {});
    expect(moved_snapshot.has_value());
    expect(!moved.active());
}

} // namespace rift_test::space_registry_epoch_provenance_00

namespace rift_test::space_registry_epoch_provenance_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "rejected drafts consume epochs and complete provenance rejects a foreign registry"_test = [] {
        check_epoch_and_provenance<2>();
        check_epoch_and_provenance<3>();
    };
}

static_assert(!std::is_invocable_v<decltype(&rift::SpaceRegistry<2>::finalize), const rift::SpaceRegistry<2>&,
                                   rift::SpaceDraft<2>, std::vector<rift::RegionalEntrySpecification>>);

} // namespace rift_test::space_registry_epoch_provenance_00
