#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>

namespace rift_test::space_registry_support_cardinality_00 {

template<int dim> void check_support_cardinality()
{
    using namespace boost::ut;

    auto fixture = rift::test::make_single_phase_space_fixture<dim>();
    rift::SpaceRegistry<dim> const registry(fixture.mesh);
    const auto schema = [&] {
        return rift::SpaceSpecification{
            .phase_fields = {{.phase = fixture.gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
            .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1}};
    };
    const rift::PhaseSupportSpecification support{
        .phase = fixture.gas, .mesh = fixture.mesh->id(), .locally_owned_requested_cells = {}};

    const auto missing = registry.begin_draft(fixture.graph, schema(), {});
    expect(rift::test::has_space_error(missing.error(), rift::SpaceBuildErrorCode::missing_phase_support));

    const auto duplicate = registry.begin_draft(fixture.graph, schema(), {support, support});
    expect(rift::test::has_space_error(duplicate.error(), rift::SpaceBuildErrorCode::duplicate_phase_support));

    auto no_fields = schema();
    no_fields.phase_fields.clear();
    const auto unused = registry.begin_draft(fixture.graph, std::move(no_fields), {support});
    expect(rift::test::has_space_error(unused.error(), rift::SpaceBuildErrorCode::unused_phase_support));

    const auto empty = registry.begin_draft(fixture.graph, schema(), {support});
    expect(empty.has_value());
    expect(empty->field_spaces().front().support().final_locally_owned_cells().empty());
}

} // namespace rift_test::space_registry_support_cardinality_00

namespace rift_test::space_registry_support_cardinality_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "phase support is exactly once per represented phase and may be empty"_test = [] {
        check_support_cardinality<2>();
        check_support_cardinality<3>();
    };
}

} // namespace rift_test::space_registry_support_cardinality_00
