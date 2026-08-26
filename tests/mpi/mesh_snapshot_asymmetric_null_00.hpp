#pragma once

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <iostream>
#include <memory>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <rift/mesh_snapshot.hpp>
#include <rift/run_configuration.hpp>

namespace rift_test::mpi::mesh_snapshot_asymmetric_null_00 {

template<int dim> bool check_asymmetric_null(const int rank)
{
    auto distributed = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    dealii::GridGenerator::hyper_cube(*distributed);
    std::unique_ptr<dealii::Triangulation<dim>> input;
    if (rank != 0) {
        input = std::move(distributed);
    }
    const auto run = rift::RunConfiguration::create(MPI_COMM_WORLD).value();

    const auto result = rift::make_mesh_snapshot(run, std::move(input));
    return !result && result.error().code == rift::MeshSnapshotErrorCode::null_triangulation && input == nullptr;
}

inline int run_test()
{
    int rank = 0;
    int size = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    if (size != 2 || !check_asymmetric_null<2>(rank) || !check_asymmetric_null<3>(rank)) {
        std::cerr << "asymmetric null mesh input was not rejected collectively\n";
        return 1;
    }
    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "asymmetric null mesh input is rejected collectively"_test = [] { expect(run_test() == 0); };
}

} // namespace rift_test::mpi::mesh_snapshot_asymmetric_null_00
