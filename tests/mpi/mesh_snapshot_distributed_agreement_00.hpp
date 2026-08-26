#pragma once

#include <boost/ut.hpp>
#include <cstdint>
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

namespace rift_test::mpi::mesh_snapshot_distributed_agreement_00 {

template<int dim> bool check_distributed_agreement()
{
    MPI_Comm source = MPI_COMM_NULL;
    if (MPI_Comm_dup(MPI_COMM_WORLD, &source) != MPI_SUCCESS) {
        return false;
    }
    std::shared_ptr<const rift::MeshSnapshot<dim>> first;
    std::shared_ptr<const rift::MeshSnapshot<dim>> second;
    {
        const auto run = rift::RunConfiguration::create(source).value();
        std::unique_ptr<dealii::Triangulation<dim>> first_mesh =
            std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(source);
        std::unique_ptr<dealii::Triangulation<dim>> second_mesh =
            std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(source);
        dealii::GridGenerator::hyper_cube(*first_mesh);
        dealii::GridGenerator::hyper_cube(*second_mesh);
        first = rift::make_mesh_snapshot(run, std::move(first_mesh)).value();
        second = rift::make_mesh_snapshot(run, std::move(second_mesh)).value();
    }
    if (MPI_Comm_free(&source) != MPI_SUCCESS || first->id() == second->id()) {
        return false;
    }

    std::uint64_t minimum = 0;
    std::uint64_t maximum = 0;
    const auto local = first->id().value();
    if (MPI_Allreduce(&local, &minimum, 1, MPI_UINT64_T, MPI_MIN, MPI_COMM_WORLD) != MPI_SUCCESS ||
        MPI_Allreduce(&local, &maximum, 1, MPI_UINT64_T, MPI_MAX, MPI_COMM_WORLD) != MPI_SUCCESS) {
        return false;
    }
    int relationship = MPI_UNEQUAL;
    const auto* distributed =
        dynamic_cast<const dealii::parallel::distributed::Triangulation<dim>*>(&first->triangulation());
    return distributed != nullptr && minimum == maximum &&
           MPI_Comm_compare(first->communicator(), MPI_COMM_WORLD, &relationship) == MPI_SUCCESS &&
           (relationship == MPI_IDENT || relationship == MPI_CONGRUENT) &&
           first->triangulation().n_global_active_cells() == 1U;
}

inline int run_test()
{
    int size = 0;
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    if (size != 2 || !check_distributed_agreement<2>() || !check_distributed_agreement<3>()) {
        std::cerr << "distributed mesh snapshot agreement or lifetime failed\n";
        return 1;
    }
    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "distributed mesh snapshots agree and retain communicator lifetime"_test = [] { expect(run_test() == 0); };
}

} // namespace rift_test::mpi::mesh_snapshot_distributed_agreement_00
