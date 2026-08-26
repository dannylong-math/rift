#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>
#include <vector>

namespace rift_test::space_registry_reject_duplicate_group_00 {

template<int dim> void check_duplicate_group_rejected()
{
    using namespace boost::ut;

    auto fixture = rift::test::make_single_phase_space_fixture<dim>();
    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = fixture.gas, .name = "flow", .components = 1, .polynomial_degree = 1},
                         {.phase = fixture.gas, .name = "flow", .components = 2, .polynomial_degree = 2}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };

    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = fixture.gas, .mesh = fixture.mesh->id(), .locally_owned_requested_cells = fixture.cells}};
    rift::SpaceRegistry<dim> const registry(fixture.mesh);
    const auto result = registry.begin_draft(fixture.graph, std::move(specification), std::move(supports));

    expect(!result.has_value());
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::duplicate_field_name));
}

} // namespace rift_test::space_registry_reject_duplicate_group_00

namespace rift_test::space_registry_reject_duplicate_group_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "one phase may not declare the same field-group name twice in 2D or 3D"_test = [] {
        check_duplicate_group_rejected<2>();
        check_duplicate_group_rejected<3>();
    };
}

} // namespace rift_test::space_registry_reject_duplicate_group_00
