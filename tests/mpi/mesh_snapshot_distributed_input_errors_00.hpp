#pragma once

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <deal.II/distributed/shared_tria.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/grid/tria.h>
#include <iostream>
#include <memory>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <rift/mesh_snapshot.hpp>
#include <rift/run_configuration.hpp>

namespace rift_test::mpi::mesh_snapshot_distributed_input_errors_00 {

template<int dim> bool check_input_errors()
{
    const auto run = rift::RunConfiguration::create(MPI_COMM_WORLD).value();

    std::unique_ptr<dealii::Triangulation<dim>> empty =
        std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    const auto empty_result = rift::make_mesh_snapshot(run, std::move(empty));
    if (empty_result || empty_result.error().code != rift::MeshSnapshotErrorCode::empty_distributed_triangulation ||
        empty != nullptr) {
        return false;
    }

    std::unique_ptr<dealii::Triangulation<dim>> unsupported =
        std::make_unique<dealii::parallel::shared::Triangulation<dim>>(MPI_COMM_WORLD);
    const auto unsupported_result = rift::make_mesh_snapshot(run, std::move(unsupported));
    return !unsupported_result &&
           unsupported_result.error().code == rift::MeshSnapshotErrorCode::unsupported_triangulation_kind &&
           unsupported == nullptr;
}

inline int run_test()
{
    int size = 0;
    if (MPI_Comm_size(MPI_COMM_WORLD, &size) != MPI_SUCCESS || size != 2 || !check_input_errors<2>() ||
        !check_input_errors<3>()) {
        std::cerr << "unsupported or empty distributed mesh was not rejected\n";
        return 1;
    }
    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "unsupported or empty distributed meshes are rejected collectively"_test = [] { expect(run_test() == 0); };
}

} // namespace rift_test::mpi::mesh_snapshot_distributed_input_errors_00
