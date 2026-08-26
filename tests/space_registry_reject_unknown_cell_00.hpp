#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <deal.II/grid/cell_id.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>
#include <vector>

namespace rift_test::space_registry_reject_unknown_cell_00 {

template<int dim> void check_unknown_cell_rejected()
{
    using namespace boost::ut;

    auto fixture = rift::test::make_single_phase_space_fixture<dim>();
    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = fixture.gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };

    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = fixture.gas, .mesh = fixture.mesh->id(), .locally_owned_requested_cells = {dealii::CellId("99_0:")}}};
    rift::SpaceRegistry<dim> const registry(fixture.mesh);
    const auto result = registry.begin_draft(fixture.graph, std::move(specification), std::move(supports));

    expect(!result.has_value());
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::unknown_support_cell));
}

} // namespace rift_test::space_registry_reject_unknown_cell_00

namespace rift_test::space_registry_reject_unknown_cell_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "support envelopes reject cells outside the background mesh in 2D and 3D"_test = [] {
        check_unknown_cell_rejected<2>();
        check_unknown_cell_rejected<3>();
    };
}

} // namespace rift_test::space_registry_reject_unknown_cell_00
