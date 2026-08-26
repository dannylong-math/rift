#pragma once

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
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

namespace rift_test::mpi::mesh_snapshot_reject_unrelated_00 {

template<int dim> class TrackedTriangulation final : public dealii::Triangulation<dim> {
public:
    explicit TrackedTriangulation(int& destruction_count) : destruction_count_(&destruction_count) {}
    ~TrackedTriangulation() override { ++*destruction_count_; }

    TrackedTriangulation(const TrackedTriangulation&) = delete;
    TrackedTriangulation(TrackedTriangulation&&) = delete;
    TrackedTriangulation& operator=(const TrackedTriangulation&) = delete;
    TrackedTriangulation& operator=(TrackedTriangulation&&) = delete;

private:
    int* destruction_count_;
};

template<int dim> bool check_unrelated_consumption()
{
    const auto run = rift::RunConfiguration::create(MPI_COMM_WORLD).value();
    int destruction_count = 0;
    std::unique_ptr<dealii::Triangulation<dim>> mesh = std::make_unique<TrackedTriangulation<dim>>(destruction_count);
    dealii::GridGenerator::hyper_cube(*mesh);

    const auto result = rift::make_mesh_snapshot(run, std::move(mesh));
    return !result && result.error().code == rift::MeshSnapshotErrorCode::communicator_mismatch && mesh == nullptr &&
           destruction_count == 1;
}

inline int run_test()
{
    int size = 0;
    if (MPI_Comm_size(MPI_COMM_WORLD, &size) != MPI_SUCCESS || size != 2 || !check_unrelated_consumption<2>() ||
        !check_unrelated_consumption<3>()) {
        std::cerr << "MPI_UNEQUAL mesh was not consumed and rejected\n";
        return 1;
    }
    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "MPI unrelated meshes are consumed and rejected collectively"_test = [] { expect(run_test() == 0); };
}

} // namespace rift_test::mpi::mesh_snapshot_reject_unrelated_00
