#pragma once

#include <boost/ut.hpp>
#include <concepts>
#include <deal.II/base/mpi.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <memory>
#include <mpi.h>
#include <rift/mesh_snapshot.hpp>
#include <rift/run_configuration.hpp>

namespace rift_test::mesh_snapshot_ownership_00 {

template<int dim> void check_immutable_ownership()
{
    auto mesh = std::make_unique<dealii::Triangulation<dim>>();
    dealii::GridGenerator::hyper_cube(*mesh);
    const auto* original = mesh.get();
    const auto run = rift::RunConfiguration::create(MPI_COMM_SELF).value();

    const auto result = rift::make_mesh_snapshot(run, std::move(mesh));

    boost::ut::expect(result.has_value());
    boost::ut::expect(mesh == nullptr);
    boost::ut::expect(&result.value()->triangulation() == original);
    boost::ut::expect(result.value()->triangulation().n_active_cells() == 1U);
    static_assert(std::same_as<decltype(result.value()->triangulation()), const dealii::Triangulation<dim>&>);
}

} // namespace rift_test::mesh_snapshot_ownership_00

namespace rift_test::mesh_snapshot_ownership_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "mesh snapshots consume and immutably own 2D and 3D triangulations"_test = [] {
        check_immutable_ownership<2>();
        check_immutable_ownership<3>();
    };
}

} // namespace rift_test::mesh_snapshot_ownership_00
