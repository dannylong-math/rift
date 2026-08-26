#pragma once

#include "../discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <concepts>
#include <deal.II/base/mpi.h>
#include <expected>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>

/**
 * \file
 * \brief Link every supported public template surface into the coverage denominator.
 */

/** Explicitly instantiate the complete supported 2D public class surface. */
template class rift::MeshSnapshot<2>;
template class rift::FieldGroupSpace<2>;
template class rift::LevelSetFieldSpace<2>;
template class rift::SpaceDraft<2>;
template class rift::SpaceSnapshot<2>;
template class rift::SpaceRegistry<2>;

/** Explicitly instantiate the complete supported 3D public class surface. */
template class rift::MeshSnapshot<3>;
template class rift::FieldGroupSpace<3>;
template class rift::LevelSetFieldSpace<3>;
template class rift::SpaceDraft<3>;
template class rift::SpaceSnapshot<3>;
template class rift::SpaceRegistry<3>;

/** Explicitly instantiate the supported 2D mesh factory. */
template std::expected<std::shared_ptr<const rift::MeshSnapshot<2>>, rift::MeshSnapshotError>
rift::make_mesh_snapshot<2>(const rift::RunConfiguration&, std::unique_ptr<dealii::Triangulation<2>>);

/** Explicitly instantiate the supported 3D mesh factory. */
template std::expected<std::shared_ptr<const rift::MeshSnapshot<3>>, rift::MeshSnapshotError>
rift::make_mesh_snapshot<3>(const rift::RunConfiguration&, std::unique_ptr<dealii::Triangulation<3>>);

static_assert(std::same_as<rift::SpaceDraftResult<2>, std::expected<rift::SpaceDraft<2>, rift::SpaceBuildErrors>>);
static_assert(std::same_as<rift::SpaceDraftResult<3>, std::expected<rift::SpaceDraft<3>, rift::SpaceBuildErrors>>);
static_assert(
    std::same_as<rift::SpaceSnapshotResult<2>, std::expected<rift::SpaceSnapshot<2>, rift::SpaceBuildErrors>>);
static_assert(
    std::same_as<rift::SpaceSnapshotResult<3>, std::expected<rift::SpaceSnapshot<3>, rift::SpaceBuildErrors>>);

namespace rift_test::coverage_completeness_guard_00 {

/** Exercise accessor definitions exposed only by the explicit template instances. */
template<int dim> [[nodiscard]] bool check_explicit_accessor_instances()
{
    const auto snapshot = rift::test::make_space_with_one_phase_field<dim>();
    const auto& field = snapshot.field_spaces().front();
    const auto& level_set = snapshot.level_set_space();

    return field.polynomial_degree() == 1 && field.epoch() == snapshot.epoch() && level_set.polynomial_degree() == 1 &&
           level_set.epoch() == snapshot.epoch() && level_set.constraints().n_constraints() == 0;
}

/** Register the complete explicit 2D/3D accessor surface check. */
inline void register_tests()
{
    using namespace boost::ut;
    "the whole-archive guard exercises every reviewed explicit template accessor"_test = [] {
        expect(check_explicit_accessor_instances<2>() && check_explicit_accessor_instances<3>());
    };
}

} // namespace rift_test::coverage_completeness_guard_00
