#pragma once

#include <array>
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

namespace rift_test::mpi::mesh_snapshot_global_ids_00 {

template<int dim>
std::shared_ptr<const rift::MeshSnapshot<dim>> make_distributed(const rift::RunConfiguration& run,
                                                                const MPI_Comm communicator)
{
    std::unique_ptr<dealii::Triangulation<dim>> mesh =
        std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(communicator);
    dealii::GridGenerator::hyper_cube(*mesh);
    return rift::make_mesh_snapshot(run, std::move(mesh)).value();
}

template<int dim> bool check_global_ids(const int rank, const int size)
{
    const auto self_run = rift::RunConfiguration::create(MPI_COMM_SELF).value();
    auto serial_mesh = std::make_unique<dealii::Triangulation<dim>>();
    dealii::GridGenerator::hyper_cube(*serial_mesh);
    const auto self_snapshot = rift::make_mesh_snapshot(self_run, std::move(serial_mesh)).value();

    std::array<std::uint64_t, 2> self_ids{};
    const std::uint64_t local_self_id = self_snapshot->id().value();
    if (MPI_Allgather(&local_self_id, 1, MPI_UINT64_T, self_ids.data(), 1, MPI_UINT64_T, MPI_COMM_WORLD) !=
            MPI_SUCCESS ||
        self_ids.at(0) == self_ids.at(1)) {
        return false;
    }

    const auto world_run = rift::RunConfiguration::create(MPI_COMM_WORLD).value();
    const auto world_snapshot = make_distributed<dim>(world_run, MPI_COMM_WORLD);

    MPI_Comm reversed = MPI_COMM_NULL;
    if (MPI_Comm_split(MPI_COMM_WORLD, 0, size - rank, &reversed) != MPI_SUCCESS) {
        return false;
    }
    const auto reversed_run = rift::RunConfiguration::create(reversed).value();
    const auto reversed_snapshot = make_distributed<dim>(reversed_run, reversed);

    std::array<std::uint64_t, 2> world_ids{};
    std::array<std::uint64_t, 2> reversed_ids{};
    const auto local_world_id = world_snapshot->id().value();
    const auto local_reversed_id = reversed_snapshot->id().value();
    const bool communication_ok = MPI_Allgather(&local_world_id, 1, MPI_UINT64_T, world_ids.data(), 1, MPI_UINT64_T,
                                                MPI_COMM_WORLD) == MPI_SUCCESS &&
                                  MPI_Allgather(&local_reversed_id, 1, MPI_UINT64_T, reversed_ids.data(), 1,
                                                MPI_UINT64_T, MPI_COMM_WORLD) == MPI_SUCCESS;
    const bool unique = world_ids.at(0) == world_ids.at(1) && reversed_ids.at(0) == reversed_ids.at(1) &&
                        world_ids.at(0) != reversed_ids.at(0) && world_ids.at(0) != self_ids.at(0) &&
                        world_ids.at(0) != self_ids.at(1) && reversed_ids.at(0) != self_ids.at(0) &&
                        reversed_ids.at(0) != self_ids.at(1);

    const bool free_ok = MPI_Comm_free(&reversed) == MPI_SUCCESS;
    return communication_ok && unique && free_ok;
}

inline int run_test()
{
    int rank = 0;
    int size = 0;
    if (MPI_Comm_rank(MPI_COMM_WORLD, &rank) != MPI_SUCCESS || MPI_Comm_size(MPI_COMM_WORLD, &size) != MPI_SUCCESS ||
        size != 2 || !check_global_ids<2>(rank, size) || !check_global_ids<3>(rank, size)) {
        std::cerr << "mesh snapshot IDs were reused across disjoint or overlapping runs\n";
        return 1;
    }
    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "mesh snapshot identities are unique across disjoint and overlapping runs"_test = [] { expect(run_test() == 0); };
}

} // namespace rift_test::mpi::mesh_snapshot_global_ids_00
