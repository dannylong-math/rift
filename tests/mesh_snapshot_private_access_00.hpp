#pragma once

#include "mesh_snapshot_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <memory>
#include <mpi.h>
#include <rift/mesh_snapshot.hpp>
#include <utility>

namespace rift_test::mesh_snapshot_private_access_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "private gateway exposes the retained run and controlled mutable mesh"_test = [] {
        const auto run = mesh_snapshot_test::make_run(17);
        auto mesh = std::make_unique<dealii::Triangulation<2>>();
        dealii::GridGenerator::hyper_cube(*mesh);
        const auto snapshot = rift::make_mesh_snapshot(run, std::move(mesh)).value();

        auto& mutable_mesh = rift::detail::MeshSnapshotAccess<2>::mutable_triangulation(*snapshot);
        mutable_mesh.refine_global(1);
        expect(snapshot->triangulation().n_active_cells() == 4_u);
        expect(rift::detail::MeshSnapshotAccess<2>::run_control(*snapshot)->id() == run.id());
        expect(snapshot->provenance() == rift::MeshSnapshotProvenance{.run = run.id(), .mesh = snapshot->id()});
    };
}

} // namespace rift_test::mesh_snapshot_private_access_00
