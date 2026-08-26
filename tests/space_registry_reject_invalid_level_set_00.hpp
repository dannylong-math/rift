#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>

namespace rift_test::space_registry_reject_invalid_level_set_00 {

template<int dim> void check_invalid_level_set_collected()
{
    using namespace boost::ut;

    auto fixture = rift::test::make_single_phase_space_fixture<dim>();
    rift::SpaceSpecification specification{
        .phase_fields = {},
        .level_set = {.name = "", .components = 0, .polynomial_degree = 0},
    };

    rift::SpaceRegistry<dim> const registry(fixture.mesh);
    const auto result = registry.begin_draft(fixture.graph, std::move(specification), {});

    expect(!result.has_value());
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::empty_level_set_name));
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::zero_level_set_components));
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::zero_level_set_polynomial_degree));
}

} // namespace rift_test::space_registry_reject_invalid_level_set_00

namespace rift_test::space_registry_reject_invalid_level_set_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "independent level-set schema errors are collected in 2D and 3D"_test = [] {
        check_invalid_level_set_collected<2>();
        check_invalid_level_set_collected<3>();
    };
}

} // namespace rift_test::space_registry_reject_invalid_level_set_00
