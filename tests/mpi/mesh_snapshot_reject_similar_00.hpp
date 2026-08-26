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

namespace rift_test::mpi::mesh_snapshot_reject_similar_00 {

template<int dim> bool check_reordered_rejection(const int rank, const int size)
{
    MPI_Comm reordered = MPI_COMM_NULL;
    if (MPI_Comm_split(MPI_COMM_WORLD, 0, size - rank, &reordered) != MPI_SUCCESS) {
        return false;
    }
    std::unique_ptr<dealii::Triangulation<dim>> mesh =
        std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(reordered);
    dealii::GridGenerator::hyper_cube(*mesh);
    const auto run = rift::RunConfiguration::create(MPI_COMM_WORLD).value();
    const auto result = rift::make_mesh_snapshot(run, std::move(mesh));
    const bool passed =
        !result && result.error().code == rift::MeshSnapshotErrorCode::communicator_reordered && mesh == nullptr;
    MPI_Comm_free(&reordered);
    return passed;
}

inline int run_test()
{
    int rank = 0;
    int size = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    if (size != 2 || !check_reordered_rejection<2>(rank, size) || !check_reordered_rejection<3>(rank, size)) {
        std::cerr << "MPI_SIMILAR mesh communicator was not rejected\n";
        return 1;
    }
    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "MPI similar mesh communicators are rejected collectively"_test = [] { expect(run_test() == 0); };
}

} // namespace rift_test::mpi::mesh_snapshot_reject_similar_00
