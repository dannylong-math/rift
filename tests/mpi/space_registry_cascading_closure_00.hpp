#pragma once

#include <algorithm>
#include <boost/ut.hpp>
#include <cstddef>
#include <deal.II/base/geometry_info.h>
#include <deal.II/base/mpi.h>
#include <deal.II/base/point.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <map>
#include <memory>
#include <mpi.h>
#include <numeric>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace rift_test::mpi::space_registry_cascading_closure_00 {

template<int dim, class Triangulation> void create_cascading_topology(Triangulation& mesh)
{
    std::vector<unsigned int> repetitions(dim, 1);
    repetitions.at(0) = 2;
    repetitions.at(1) = 2;
    dealii::Point<dim> upper;
    for (unsigned int direction = 0; std::cmp_less(direction, dim); ++direction) {
        upper(direction) = 1.0;
    }
    dealii::GridGenerator::subdivided_hyper_rectangle(mesh, repetitions, dealii::Point<dim>(), upper);
    for (const auto& cell : mesh.active_cell_iterators()) {
        if (cell->is_locally_owned() && cell->center()(0) > 0.5) {
            cell->set_refine_flag();
        }
    }
    mesh.execute_coarsening_and_refinement();
    for (const auto& cell : mesh.active_cell_iterators()) {
        if (cell->is_locally_owned() && cell->center()(0) > 0.5 && cell->center()(1) > 0.5 &&
            cell->center()(1) < 0.75) {
            cell->set_refine_flag();
        }
    }
    mesh.execute_coarsening_and_refinement();
}

struct LocalTopology {
    std::vector<std::string> active;
    std::vector<std::string> interfaces;
};

template<int dim> LocalTopology collect_local_topology(const dealii::Triangulation<dim>& mesh)
{
    LocalTopology result;
    for (const auto& cell : mesh.active_cell_iterators()) {
        if (cell->is_locally_owned()) {
            result.active.push_back(cell->id().to_string());
        }
        if (cell->is_artificial()) {
            continue;
        }
        for (unsigned int face = 0; face < dealii::GeometryInfo<dim>::faces_per_cell; ++face) {
            if (cell->at_boundary(face) || !cell->neighbor_is_coarser(face)) {
                continue;
            }
            const auto [coarse_face, subface] = cell->neighbor_of_coarser_neighbor(face);
            static_cast<void>(subface);
            const auto coarse = cell->neighbor(face);
            if (coarse->is_artificial()) {
                continue;
            }
            std::vector<std::string> ids{coarse->id().to_string()};
            for (unsigned int child = 0; child < dealii::GeometryInfo<dim>::max_children_per_face; ++child) {
                const auto fine = coarse->neighbor_child_on_subface(coarse_face, child);
                if (!fine->is_artificial()) {
                    ids.push_back(fine->id().to_string());
                }
            }
            std::ranges::sort(ids);
            std::string record;
            for (const auto& id : ids) {
                record += std::to_string(id.size()) + ":" + id;
            }
            result.interfaces.push_back(std::move(record));
        }
    }
    return result;
}

struct GlobalTopology {
    std::set<std::string> active;
    std::map<std::string, int> owners;
    std::vector<std::set<std::string>> interfaces;
};

inline GlobalTopology merge_topology(const LocalTopology& local)
{
    GlobalTopology result;
    const auto active_by_rank = dealii::Utilities::MPI::all_gather(MPI_COMM_WORLD, local.active);
    for (std::size_t owner = 0; owner < active_by_rank.size(); ++owner) {
        const auto& ids = active_by_rank.at(owner);
        result.active.insert(ids.begin(), ids.end());
        for (const auto& id : ids) {
            result.owners.emplace(id, static_cast<int>(owner));
        }
    }
    std::set<std::string> encoded_interfaces;
    for (const auto& records : dealii::Utilities::MPI::all_gather(MPI_COMM_WORLD, local.interfaces)) {
        encoded_interfaces.insert(records.begin(), records.end());
    }
    for (const auto& record : encoded_interfaces) {
        std::set<std::string> ids;
        std::size_t cursor = 0;
        while (cursor < record.size()) {
            const auto separator = record.find(':', cursor);
            const auto size = static_cast<std::size_t>(std::stoull(record.substr(cursor, separator - cursor)));
            cursor = separator + 1;
            ids.insert(record.substr(cursor, size));
            cursor += size;
        }
        result.interfaces.push_back(std::move(ids));
    }
    return result;
}

struct CascadingOracle {
    std::set<std::string> closed_mask;
    std::string seed;
    unsigned int growth_steps{};
};

inline CascadingOracle find_cascading_oracle(const GlobalTopology& topology)
{
    for (const auto& candidate : topology.active) {
        std::set<std::string> mask{candidate};
        unsigned int growth_steps = 0;
        while (true) {
            const auto old_size = mask.size();
            std::set<std::string> additions;
            for (const auto& interface : topology.interfaces) {
                if (std::ranges::any_of(interface, [&](const auto& id) { return mask.contains(id); })) {
                    additions.insert(interface.begin(), interface.end());
                }
            }
            mask.insert(additions.begin(), additions.end());
            if (mask.size() == old_size) {
                break;
            }
            ++growth_steps;
        }
        std::set<int> participating_owners;
        for (const auto& id : mask) {
            participating_owners.insert(topology.owners.at(id));
        }
        if (growth_steps > 1 && participating_owners.size() > 1) {
            return {.closed_mask = std::move(mask), .seed = candidate, .growth_steps = growth_steps};
        }
    }
    return {};
}

template<int dim> void check_distributed_cascade()
{
    using namespace boost::ut;
    auto run = rift::RunConfiguration::create(MPI_COMM_WORLD).value();
    auto graph = rift::make_phase_graph(run, {{"gas", "compressible"}}, {}).value();
    const auto gas_id = graph.find_phase("gas");
    expect(gas_id.has_value());
    if (!gas_id) {
        return;
    }
    const auto gas_result = graph.reference(*gas_id);
    expect(gas_result.has_value());
    if (!gas_result) {
        return;
    }
    const auto gas = *gas_result;
    auto distributed = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    create_cascading_topology<dim>(*distributed);
    const auto topology = merge_topology(collect_local_topology<dim>(*distributed));
    const auto oracle = find_cascading_oracle(topology);
    expect(!oracle.seed.empty());
    expect(oracle.growth_steps > 1_u);
    if (oracle.seed.empty()) {
        return;
    }
    const int seed_owner = topology.owners.at(oracle.seed);
    rift::SupportEnvelope requested_local;
    for (const auto& cell : distributed->active_cell_iterators()) {
        if (cell->is_locally_owned() && cell->id().to_string() == oracle.seed) {
            requested_local.insert(cell->id());
        }
    }
    const auto requested_by_rank =
        dealii::Utilities::MPI::all_gather(MPI_COMM_WORLD, static_cast<unsigned int>(requested_local.size()));
    expect(std::accumulate(requested_by_rank.begin(), requested_by_rank.end(), 0U) == 1_u);
    expect(requested_by_rank.at(static_cast<std::size_t>(seed_owner)) == 1_u);
    std::unique_ptr<dealii::Triangulation<dim>> consumed = std::move(distributed);
    auto mesh = rift::make_mesh_snapshot(run, std::move(consumed)).value();
    rift::SpaceRegistry<dim> const registry(mesh);
    const auto specification = [&] {
        return rift::SpaceSpecification{
            .phase_fields = {{.phase = gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
            .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
        };
    };
    const auto supports = [&] {
        return std::vector<rift::PhaseSupportSpecification>{
            {.phase = gas, .mesh = mesh->id(), .locally_owned_requested_cells = requested_local}};
    };
    const auto limited = rift::detail::SpaceRegistryAccess<dim>::begin_draft(
        registry, graph, specification(), supports(), rift::SpaceEpoch::from_index(91), 1);
    expect(!limited.has_value());
    expect(std::ranges::any_of(limited.error(), [](const auto& error) {
        return error.code == rift::SpaceBuildErrorCode::closure_nonconvergence;
    }));

    const auto draft = registry.begin_draft(graph, specification(), supports());
    expect(draft.has_value());
    std::vector<std::string> local_ids;
    for (const auto& id : draft->field_spaces().front().support().final_locally_owned_cells()) {
        local_ids.push_back(id.to_string());
    }
    std::set<std::string> actual;
    for (const auto& ids : dealii::Utilities::MPI::all_gather(MPI_COMM_WORLD, local_ids)) {
        for (const auto& id : ids) {
            actual.emplace(id);
        }
    }
    expect(actual == oracle.closed_mask);
    std::set<int> actual_owners;
    bool added_on_different_owner = false;
    for (const auto& id : actual) {
        actual_owners.insert(topology.owners.at(id));
        if (id != oracle.seed && topology.owners.at(id) != seed_owner) {
            added_on_different_owner = true;
        }
    }
    expect(actual_owners.size() > 1_u);
    expect(added_on_different_owner);
}

inline void register_tests()
{
    using namespace boost::ut;
    "distributed multilevel closure reaches the serial least fixed point in 2D and 3D"_test = [] {
        check_distributed_cascade<2>();
        check_distributed_cascade<3>();
    };
}

} // namespace rift_test::mpi::space_registry_cascading_closure_00
