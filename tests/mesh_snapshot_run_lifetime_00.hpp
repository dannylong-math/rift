#pragma once

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <memory>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <rift/mesh_snapshot.hpp>
#include <rift/run_configuration.hpp>

namespace rift_test::mesh_snapshot_run_lifetime_00 {

template<int dim> void check_retained_run_lifetime()
{
    MPI_Comm source = MPI_COMM_NULL;
    boost::ut::expect(MPI_Comm_dup(MPI_COMM_SELF, &source) == MPI_SUCCESS);
    std::shared_ptr<const rift::MeshSnapshot<dim>> snapshot;
    rift::RunConfigurationId run_id = rift::RunConfigurationId::from_index(0);
    {
        auto run = rift::RunConfiguration::create(source).value();
        run_id = run.id();
        auto mesh = std::make_unique<dealii::Triangulation<dim>>();
        dealii::GridGenerator::hyper_cube(*mesh);
        snapshot = rift::make_mesh_snapshot(run, std::move(mesh)).value();
    }
    boost::ut::expect(MPI_Comm_free(&source) == MPI_SUCCESS);

    int relationship = MPI_UNEQUAL;
    boost::ut::expect(MPI_Comm_compare(snapshot->communicator(), MPI_COMM_SELF, &relationship) == MPI_SUCCESS);
    boost::ut::expect(relationship == MPI_IDENT || relationship == MPI_CONGRUENT);
    boost::ut::expect(snapshot->provenance().run == run_id);
    boost::ut::expect(snapshot->triangulation().n_active_cells() == 1U);
}

} // namespace rift_test::mesh_snapshot_run_lifetime_00

namespace rift_test::mesh_snapshot_run_lifetime_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "mesh snapshots retain run and communicator lifetime in 2D and 3D"_test = [] {
        check_retained_run_lifetime<2>();
        check_retained_run_lifetime<3>();
    };
}

} // namespace rift_test::mesh_snapshot_run_lifetime_00
