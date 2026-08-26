#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>
#include <vector>

namespace rift_test::space_registry_reject_regions_00 {

template<int dim> void check_invalid_regions_rejected()
{
    using namespace boost::ut;

    auto fixture = rift::test::make_single_phase_space_fixture<dim>();
    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = fixture.gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = fixture.gas, .mesh = fixture.mesh->id(), .locally_owned_requested_cells = fixture.cells}};
    rift::SpaceRegistry<dim> const registry(fixture.mesh);
    auto draft = registry.begin_draft(fixture.graph, std::move(specification), std::move(supports));
    const auto result = registry.finalize(draft.value(), {{""}, {"pressure"}, {"pressure"}});
    expect(draft->active());

    expect(!result.has_value());
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::empty_regional_entry_name));
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::duplicate_regional_entry_name));
    const auto recovered = registry.finalize(draft.value(), {{"pressure"}});
    expect(recovered.has_value());
    expect(!draft->active());
}

} // namespace rift_test::space_registry_reject_regions_00

namespace rift_test::space_registry_reject_regions_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "invalid regional schemas are rejected during 2D and 3D finalization"_test = [] {
        check_invalid_regions_rejected<2>();
        check_invalid_regions_rejected<3>();
    };
}

} // namespace rift_test::space_registry_reject_regions_00
