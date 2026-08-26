#pragma once

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <memory>
#include <mpi.h>
#include <rift/mesh_snapshot.hpp>
#include <rift/run_configuration.hpp>

namespace rift_test::mesh_snapshot_distinct_id_00 {

template<int dim> void check_distinct_identity()
{
    const auto run = rift::RunConfiguration::create(MPI_COMM_SELF).value();
    auto first_mesh = std::make_unique<dealii::Triangulation<dim>>();
    auto second_mesh = std::make_unique<dealii::Triangulation<dim>>();
    dealii::GridGenerator::hyper_cube(*first_mesh);
    dealii::GridGenerator::hyper_cube(*second_mesh);

    const auto first = rift::make_mesh_snapshot(run, std::move(first_mesh)).value();
    const auto second = rift::make_mesh_snapshot(run, std::move(second_mesh)).value();

    boost::ut::expect(first->id() != second->id());
    boost::ut::expect(first->provenance().run == run.id());
    boost::ut::expect(second->provenance().run == run.id());
    boost::ut::expect(first->provenance() == first->provenance());
    boost::ut::expect(first->provenance() != second->provenance());
    const auto another_run = first->provenance();
    boost::ut::expect(first->provenance() != rift::MeshSnapshotProvenance{.run = rift::RunConfigurationId::from_index(
                                                                              another_run.run.value() + 1U),
                                                                          .mesh = another_run.mesh});
}

} // namespace rift_test::mesh_snapshot_distinct_id_00

namespace rift_test::mesh_snapshot_distinct_id_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "separately consumed equal-topology meshes have distinct 2D and 3D identities"_test = [] {
        check_distinct_identity<2>();
        check_distinct_identity<3>();
    };
}

} // namespace rift_test::mesh_snapshot_distinct_id_00
