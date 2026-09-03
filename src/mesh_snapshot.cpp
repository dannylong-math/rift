/**
 * \file
 * \brief Collective construction of immutable distributed mesh snapshots.
 */

#include <cstddef>
#include <cstdint>
#include <deal.II/base/exceptions.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/fe/mapping.h>
#include <expected>
#include <format>
#include <memory>
#include <mpi.h>
#include <rift/mesh_snapshot.hpp>
#include <rift/rift_context.hpp>
#include <string>
#include <utility>
#include <vector>

namespace rift {

namespace {

/** \brief Fixed fields exchanged for collective input validation. */
using MeshValidationRecord = std::vector<unsigned int>;

/** \brief Position of each field in a mesh validation record. */
enum class MeshValidationField : std::uint8_t {
    dimension,
    has_triangulation,
    has_mapping,
    uses_world_communicator,
};

/** \brief Convert a validation field to its record index. */
[[nodiscard]] constexpr std::size_t field_index(const MeshValidationField field) noexcept
{
    return static_cast<std::size_t>(field);
}

/** \brief Test whether one triangulation uses the exact world communicator. */
template<int dim>
[[nodiscard]] bool
uses_mpi_world(const std::unique_ptr<dealii::parallel::distributed::Triangulation<dim>>& triangulation)
{
    if (triangulation == nullptr) {
        return false;
    }

    int relationship = MPI_UNEQUAL;
    const int status = MPI_Comm_compare(triangulation->get_mpi_communicator(), MPI_COMM_WORLD, &relationship);
    if (status != MPI_SUCCESS) {
        throw dealii::ExcMPI(status);
    }
    return relationship == MPI_IDENT;
}

/** \brief Describe the local inputs with fixed, serialization-friendly fields. */
template<int dim>
[[nodiscard]] MeshValidationRecord
make_validation_record(const std::unique_ptr<dealii::parallel::distributed::Triangulation<dim>>& triangulation,
                       const std::unique_ptr<dealii::Mapping<dim>>& mapping)
{
    return {static_cast<unsigned int>(dim), static_cast<unsigned int>(triangulation != nullptr),
            static_cast<unsigned int>(mapping != nullptr), static_cast<unsigned int>(uses_mpi_world(triangulation))};
}

/** \brief Append one structured rank-local validation error. */
void add_error(MeshSnapshotErrors& errors, const MeshSnapshotErrorCode code, const unsigned int rank,
               std::string message)
{
    errors.push_back({.code = code, .rank = rank, .message = std::move(message)});
}

/** \brief Convert all gathered validation failures to deterministic errors. */
[[nodiscard]] MeshSnapshotErrors collect_errors(const std::vector<MeshValidationRecord>& records)
{
    MeshSnapshotErrors errors;
    const auto expected_dimension = records.front().at(field_index(MeshValidationField::dimension));

    for (std::size_t rank_index = 0; rank_index < records.size(); ++rank_index) {
        const auto rank = static_cast<unsigned int>(rank_index);
        const auto& record = records.at(rank_index);

        if (record.at(field_index(MeshValidationField::has_triangulation)) == 0U) {
            add_error(errors, MeshSnapshotErrorCode::null_triangulation, rank,
                      std::format("rank {} supplied no distributed triangulation", rank));
        }
        if (record.at(field_index(MeshValidationField::has_mapping)) == 0U) {
            add_error(errors, MeshSnapshotErrorCode::null_mapping, rank,
                      std::format("rank {} supplied no mapping", rank));
        }
        if (record.at(field_index(MeshValidationField::has_triangulation)) != 0U &&
            record.at(field_index(MeshValidationField::uses_world_communicator)) == 0U) {
            add_error(errors, MeshSnapshotErrorCode::communicator_mismatch, rank,
                      std::format("rank {} triangulation does not use MPI_COMM_WORLD", rank));
        }
        if (record.at(field_index(MeshValidationField::dimension)) != expected_dimension) {
            add_error(errors, MeshSnapshotErrorCode::dimension_mismatch, rank,
                      std::format("rank {} requested mesh dimension {}, but rank 0 requested {}", rank,
                                  record.at(field_index(MeshValidationField::dimension)), expected_dimension));
        }
    }
    return errors;
}

} // namespace

template<int dim>
    requires(dim == 2 || dim == 3)
MeshSnapshot<dim>::MeshSnapshot(const MeshSnapshotId id,
                                std::unique_ptr<dealii::parallel::distributed::Triangulation<dim>> triangulation,
                                std::unique_ptr<dealii::Mapping<dim>> mapping) noexcept :
    id_(id), triangulation_(std::move(triangulation)), mapping_(std::move(mapping))
{
}

template<int dim>
    requires(dim == 2 || dim == 3)
MeshSnapshotResult<dim>
RiftContext::create_mesh_snapshot(std::unique_ptr<dealii::parallel::distributed::Triangulation<dim>> triangulation,
                                  std::unique_ptr<dealii::Mapping<dim>> mapping)
{
    const auto local_record = make_validation_record(triangulation, mapping);
    const auto records = dealii::Utilities::MPI::all_gather(mpi_communicator(), local_record);
    auto errors = collect_errors(records);
    if (!errors.empty()) {
        return std::unexpected(std::move(errors));
    }

    const auto id = MeshSnapshotId::from_index(next_mesh_snapshot_index_);
    std::shared_ptr<const MeshSnapshot<dim>> snapshot(
        new MeshSnapshot<dim>(id, std::move(triangulation), std::move(mapping)));
    ++next_mesh_snapshot_index_;
    return snapshot;
}

template MeshSnapshotResult<2>
    RiftContext::create_mesh_snapshot<2>(std::unique_ptr<dealii::parallel::distributed::Triangulation<2>>,
                                         std::unique_ptr<dealii::Mapping<2>>);

template MeshSnapshotResult<3>
    RiftContext::create_mesh_snapshot<3>(std::unique_ptr<dealii::parallel::distributed::Triangulation<3>>,
                                         std::unique_ptr<dealii::Mapping<3>>);

} // namespace rift
