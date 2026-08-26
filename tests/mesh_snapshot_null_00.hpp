#pragma once

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <deal.II/grid/tria.h>
#include <memory>
#include <mpi.h>
#include <rift/mesh_snapshot.hpp>
#include <rift/run_configuration.hpp>

namespace rift_test::mesh_snapshot_null_00 {

template<int dim> void check_null_mesh()
{
    const auto run = rift::RunConfiguration::create(MPI_COMM_SELF).value();
    std::unique_ptr<dealii::Triangulation<dim>> mesh;
    const auto result = rift::make_mesh_snapshot(run, std::move(mesh));
    boost::ut::expect(!result.has_value());
    boost::ut::expect(result.error().code == rift::MeshSnapshotErrorCode::null_triangulation);
}

} // namespace rift_test::mesh_snapshot_null_00

namespace rift_test::mesh_snapshot_null_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "null 2D and 3D triangulations are rejected collectively"_test = [] {
        check_null_mesh<2>();
        check_null_mesh<3>();
    };
}

} // namespace rift_test::mesh_snapshot_null_00
