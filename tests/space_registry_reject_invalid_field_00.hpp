#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>
#include <vector>

namespace rift_test::space_registry_reject_invalid_field_00 {

template<int dim> void check_invalid_fields_collected()
{
    using namespace boost::ut;

    auto fixture = rift::test::make_single_phase_space_fixture<dim>();
    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = fixture.gas, .name = "", .components = 0, .polynomial_degree = 0}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };

    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = fixture.gas, .mesh = fixture.mesh->id(), .locally_owned_requested_cells = {}}};
    rift::SpaceRegistry<dim> const registry(fixture.mesh);
    const auto result = registry.begin_draft(fixture.graph, std::move(specification), std::move(supports));

    expect(!result.has_value());
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::empty_field_name));
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::zero_components));
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::zero_polynomial_degree));
}

} // namespace rift_test::space_registry_reject_invalid_field_00

namespace rift_test::space_registry_reject_invalid_field_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "independent field-schema errors are collected in 2D and 3D"_test = [] {
        check_invalid_fields_collected<2>();
        check_invalid_fields_collected<3>();
    };
}

} // namespace rift_test::space_registry_reject_invalid_field_00
