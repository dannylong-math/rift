/**
 * \file
 * \brief Immutable state views and compact phase-label storage.
 */

#include "state_internal.hpp"

#include <cstdint>
#include <deal.II/base/array_view.h>
#include <deal.II/base/index_set.h>
// Explicit definitions are required for the non-preinstantiated uint32_t exchange.
#include <deal.II/base/mpi_noncontiguous_partitioner.templates.h> // NOLINT(misc-include-cleaner)
#include <deal.II/base/types.h>
#include <deal.II/lac/la_parallel_vector.h>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <rift/phase_graph.hpp>
#include <rift/space_draft.hpp>
#include <rift/space_snapshot.hpp>
#include <rift/state_snapshot.hpp>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rift {

namespace {

/** \brief Compact sentinel distinct from every representable phase ID. */
constexpr std::uint32_t unassigned_phase_code = std::numeric_limits<std::uint32_t>::max();

/** \brief Reject a reference created by another finalized space. */
void validate_space_epoch(const SpaceEpoch expected, const SpaceEpoch supplied, const char* category)
{
    if (supplied != expected) {
        throw std::invalid_argument(std::format("{} reference belongs to space epoch {}, expected {}", category,
                                                supplied.value(), expected.value()));
    }
}

/** \brief Decode the hidden compact representation. */
[[nodiscard]] std::optional<PhaseId> decode_phase(const std::uint32_t code)
{
    return code == unassigned_phase_code ? std::nullopt : std::optional{PhaseId::from_index(code)};
}

} // namespace

PhaseLabelMetadata::PhaseLabelMetadata(std::unique_ptr<Impl> implementation) noexcept :
    implementation_(std::move(implementation))
{
}

PhaseLabelMetadata::~PhaseLabelMetadata() = default;

PhaseLabelMetadata::PhaseLabelMetadata(const PhaseLabelMetadata& other) :
    implementation_(std::make_unique<Impl>(*other.implementation_))
{
}

PhaseLabelMetadata& PhaseLabelMetadata::operator=(const PhaseLabelMetadata& other)
{
    if (this != &other) {
        implementation_ = std::make_unique<Impl>(*other.implementation_);
    }
    return *this;
}

PhaseLabelMetadata::PhaseLabelMetadata(PhaseLabelMetadata&&) noexcept = default;
PhaseLabelMetadata& PhaseLabelMetadata::operator=(PhaseLabelMetadata&&) noexcept = default;

const dealii::IndexSet& PhaseLabelMetadata::locally_owned_dofs() const noexcept
{
    return implementation_->locally_owned_dofs;
}

const dealii::IndexSet& PhaseLabelMetadata::ghost_dofs() const noexcept { return implementation_->ghost_dofs; }

bool PhaseLabelMetadata::contains(const dealii::types::global_dof_index dof) const
{
    if (dof >= implementation_->locally_owned_dofs.size()) {
        return false;
    }
    return implementation_->locally_owned_dofs.is_element(dof) || implementation_->ghost_dofs.is_element(dof);
}

bool PhaseLabelMetadata::locally_owns(const dealii::types::global_dof_index dof) const
{
    return dof < implementation_->locally_owned_dofs.size() && implementation_->locally_owned_dofs.is_element(dof);
}

std::optional<PhaseId> PhaseLabelMetadata::label(const dealii::types::global_dof_index dof) const
{
    if (locally_owns(dof)) {
        return decode_phase(
            implementation_->owned_values.at(implementation_->locally_owned_dofs.index_within_set(dof)));
    }
    if (dof < implementation_->ghost_dofs.size() && implementation_->ghost_dofs.is_element(dof)) {
        return decode_phase(implementation_->ghost_values.at(implementation_->ghost_dofs.index_within_set(dof)));
    }
    throw std::out_of_range(std::format("phase-label metadata does not store native DoF index {}", dof));
}

void PhaseLabelMetadata::update_ghost_values()
{
    implementation_->partitioner->export_to_ghosted_array(
        dealii::make_array_view(static_cast<const std::vector<std::uint32_t>&>(implementation_->owned_values)),
        dealii::make_array_view(implementation_->ghost_values));
}

std::span<const std::uint32_t> PhaseLabelMetadata::owned_codes() const noexcept
{
    return implementation_->owned_values;
}

MutablePhaseLabelMetadataView::MutablePhaseLabelMetadataView(PhaseLabelMetadata& metadata) noexcept :
    metadata_(&metadata)
{
}

const dealii::IndexSet& MutablePhaseLabelMetadataView::locally_owned_dofs() const noexcept
{
    return metadata_->locally_owned_dofs();
}

std::optional<PhaseId> MutablePhaseLabelMetadataView::label(const dealii::types::global_dof_index dof) const
{
    return metadata_->label(dof);
}

void MutablePhaseLabelMetadataView::set_label(const dealii::types::global_dof_index dof,
                                              const std::optional<PhaseId> phase)
{
    if (!metadata_->locally_owns(dof)) {
        throw std::out_of_range(std::format("native DoF index {} is not owned by this phase-label block", dof));
    }
    if (phase.has_value() && phase->value() >= metadata_->implementation_->phase_count) {
        throw std::invalid_argument(
            std::format("phase {} is outside this metadata block's canonical phase range [0, {})", phase->value(),
                        metadata_->implementation_->phase_count));
    }
    metadata_->implementation_->owned_values.at(metadata_->implementation_->locally_owned_dofs.index_within_set(dof)) =
        phase.has_value() ? phase->value() : unassigned_phase_code;
}

template<int dim>
    requires(dim == 2 || dim == 3)
StateSnapshot<dim>::StateSnapshot(std::shared_ptr<const SpaceSnapshot<dim>> space, StateSnapshotStamp stamp,
                                  std::unique_ptr<detail::StateStorage<dim>> storage) noexcept :
    space_(std::move(space)), stamp_(stamp), storage_(std::move(storage))
{
}

template<int dim>
    requires(dim == 2 || dim == 3)
StateSnapshot<dim>::~StateSnapshot() = default;

template<int dim>
    requires(dim == 2 || dim == 3)
const StateSnapshotStamp& StateSnapshot<dim>::stamp() const noexcept
{
    return stamp_;
}

template<int dim>
    requires(dim == 2 || dim == 3)
StateSnapshotReference StateSnapshot<dim>::reference() const noexcept
{
    return {.store_id = stamp_.store_id, .snapshot_id = stamp_.snapshot_id};
}

template<int dim>
    requires(dim == 2 || dim == 3)
const SpaceSnapshot<dim>& StateSnapshot<dim>::space() const noexcept
{
    return *space_;
}

template<int dim>
    requires(dim == 2 || dim == 3)
const dealii::LinearAlgebra::distributed::Vector<double>&
StateSnapshot<dim>::phase_support_field(const PhaseSupportFieldReference reference) const
{
    validate_space_epoch(stamp_.space_epoch, reference.space_epoch, "phase-support field");
    if (reference.field_group.value() >= storage_->phase_support_fields.size()) {
        throw std::out_of_range(std::format("phase-support field ID {} is outside [0, {})",
                                            reference.field_group.value(), storage_->phase_support_fields.size()));
    }
    return storage_->phase_support_fields.at(reference.field_group.value());
}

template<int dim>
    requires(dim == 2 || dim == 3)
const dealii::LinearAlgebra::distributed::Vector<double>&
StateSnapshot<dim>::geometry_field(const GeometryFieldReference reference) const
{
    validate_space_epoch(stamp_.space_epoch, reference.space_epoch, "geometry field");
    if (reference.field_group.value() >= storage_->geometry_fields.size()) {
        throw std::out_of_range(std::format("geometry field ID {} is outside [0, {})", reference.field_group.value(),
                                            storage_->geometry_fields.size()));
    }
    return storage_->geometry_fields.at(reference.field_group.value());
}

template<int dim>
    requires(dim == 2 || dim == 3)
const PhaseLabelMetadata&
StateSnapshot<dim>::discrete_geometry_metadata(const DiscreteGeometryMetadataReference reference) const
{
    validate_space_epoch(stamp_.space_epoch, reference.space_epoch, "discrete geometry metadata");
    if (reference.metadata.value() >= storage_->discrete_geometry_metadata.size()) {
        throw std::out_of_range(std::format("discrete geometry-metadata ID {} is outside [0, {})",
                                            reference.metadata.value(), storage_->discrete_geometry_metadata.size()));
    }
    return storage_->discrete_geometry_metadata.at(reference.metadata.value());
}

template<int dim>
    requires(dim == 2 || dim == 3)
double StateSnapshot<dim>::regional(const RegionalEntryReference reference) const
{
    validate_space_epoch(stamp_.space_epoch, reference.space_epoch, "regional entry");
    if (reference.regional_entry.value() >= storage_->regional_values.size()) {
        throw std::out_of_range(std::format("regional entry ID {} is outside [0, {})", reference.regional_entry.value(),
                                            storage_->regional_values.size()));
    }
    return storage_->regional_values.at(reference.regional_entry.value());
}

template class StateSnapshot<2>;
template class StateSnapshot<3>;

} // namespace rift
