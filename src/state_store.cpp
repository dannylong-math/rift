/**
 * \file
 * \brief Collective state allocation, retention, and publication.
 */

#include "state_internal.hpp"

#include <algorithm>
#include <array>
#include <boost/serialization/string.hpp> // NOLINT(misc-include-cleaner): instantiates Boost archive support.
#include <boost/serialization/vector.hpp> // NOLINT(misc-include-cleaner): instantiates all_gather payload support.
#include <cstdint>
#include <deal.II/base/array_view.h>
#include <deal.II/base/mpi.h>
#include <deal.II/base/mpi_noncontiguous_partitioner.h>
#include <deal.II/base/types.h>
#include <deal.II/dofs/dof_tools.h>
#include <deal.II/fe/component_mask.h>
#include <expected>
#include <format>
#include <iterator>
#include <limits>
#include <memory>
#include <mpi.h>
#include <optional>
#include <rift/field_group_space.hpp>
#include <rift/rift_context.hpp>
#include <rift/space_snapshot.hpp>
#include <rift/state_snapshot.hpp>
#include <rift/state_store.hpp>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace rift {

namespace {

/** \brief Compact sentinel distinct from every representable phase ID. */
constexpr std::uint32_t unassigned_phase_code = std::numeric_limits<std::uint32_t>::max();

/** \brief Identify one collective store transition in its agreement record. */
enum class StoreOperation : std::uint8_t { publish = 1, pin = 2, unpin = 3, discard = 4 };

/** \brief Serialization-friendly store-construction diagnostic. */
struct StoreCreationErrorWire {
    /** \brief Encoded creation-error classification. */
    std::uint8_t code = 0;
    /** \brief Rank that observed the error. */
    unsigned int rank = 0;
    /** \brief Human-readable error detail. */
    std::string message;

    /** \brief Serialize one creation diagnostic for failure-only exchange. */
    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & code;
        archive & rank;
        archive & message;
    }
};

/** \brief Serialization-friendly state diagnostic. */
struct StateErrorWire {
    /** \brief Encoded state-error classification. */
    std::uint8_t code = 0;
    /** \brief Rank that observed the error. */
    unsigned int rank = 0;
    /** \brief Encoded store identity. */
    std::uint64_t store_id = 0;
    /** \brief Encoded snapshot identity. */
    std::uint64_t snapshot_id = 0;
    /** \brief Human-readable error detail. */
    std::string message;

    /** \brief Serialize one state diagnostic for failure-only exchange. */
    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & code;
        archive & rank;
        archive & store_id;
        archive & snapshot_id;
        archive & message;
    }
};

/** \brief Order construction errors deterministically. */
void sort_creation_errors(StateStoreCreationErrors& errors)
{
    std::ranges::sort(errors, [](const auto& left, const auto& right) {
        return std::tuple{left.rank, left.code, left.message} < std::tuple{right.rank, right.code, right.message};
    });
}

/** \brief Gather complete construction diagnostics only on failure. */
[[nodiscard]] StateStoreCreationErrors collect_creation_errors(const MPI_Comm communicator,
                                                               StateStoreCreationErrors local_errors)
{
    std::vector<StoreCreationErrorWire> local_wire;
    local_wire.reserve(local_errors.size());
    std::ranges::transform(local_errors, std::back_inserter(local_wire), [](auto& error) {
        return StoreCreationErrorWire{
            .code = static_cast<std::uint8_t>(error.code), .rank = error.rank, .message = std::move(error.message)};
    });
    const auto gathered = dealii::Utilities::MPI::all_gather(communicator, local_wire);
    StateStoreCreationErrors errors;
    for (const auto& rank_errors : gathered) {
        std::ranges::transform(rank_errors, std::back_inserter(errors), [](auto error) {
            return StateStoreCreationError{.code = static_cast<StateStoreCreationErrorCode>(error.code),
                                           .rank = error.rank,
                                           .message = std::move(error.message)};
        });
    }
    sort_creation_errors(errors);
    return errors;
}

/** \brief Order state errors deterministically. */
void sort_state_errors(StateErrors& errors)
{
    std::ranges::sort(errors, [](const auto& left, const auto& right) {
        const auto& left_snapshot = left.snapshot.value();
        const auto& right_snapshot = right.snapshot.value();
        return std::tuple{left.rank, left.code, left_snapshot.store_id, left_snapshot.snapshot_id, left.message} <
               std::tuple{right.rank, right.code, right_snapshot.store_id, right_snapshot.snapshot_id, right.message};
    });
}

/** \brief Gather complete state diagnostics only on failure. */
[[nodiscard]] StateErrors collect_state_errors(const MPI_Comm communicator, StateErrors local_errors)
{
    std::vector<StateErrorWire> local_wire;
    local_wire.reserve(local_errors.size());
    std::ranges::transform(local_errors, std::back_inserter(local_wire), [](auto& error) {
        return StateErrorWire{.code = static_cast<std::uint8_t>(error.code),
                              .rank = error.rank,
                              .store_id = error.snapshot.value().store_id.value(),
                              .snapshot_id = error.snapshot.value().snapshot_id.value(),
                              .message = std::move(error.message)};
    });
    const auto gathered = dealii::Utilities::MPI::all_gather(communicator, local_wire);
    StateErrors errors;
    for (const auto& rank_errors : gathered) {
        std::ranges::transform(rank_errors, std::back_inserter(errors), [](auto error) {
            return StateError{.code = static_cast<StateErrorCode>(error.code),
                              .rank = error.rank,
                              .snapshot =
                                  StateSnapshotReference{.store_id = StateStoreId::from_index(error.store_id),
                                                         .snapshot_id = StateSnapshotId::from_index(error.snapshot_id)},
                              .message = std::move(error.message)};
        });
    }
    sort_state_errors(errors);
    return errors;
}

/** \brief Add one state error at the calling rank. */
void add_state_error(StateErrors& errors, const StateErrorCode code, const unsigned int rank,
                     const StateSnapshotReference snapshot, std::string message)
{
    errors.push_back({.code = code, .rank = rank, .snapshot = snapshot, .message = std::move(message)});
}

/** \brief Derive component-filtered locally relevant native DoF indices. */
template<int dim>
[[nodiscard]] dealii::IndexSet component_relevant_dofs(const GeometryFieldGroupSpace<dim>& field_space,
                                                       const unsigned int component)
{
    const auto& dof_handler = field_space.dof_handler();
    const auto& finite_element = field_space.finite_element();
    dealii::IndexSet selected(dof_handler.n_dofs());
    std::vector<dealii::types::global_dof_index> dof_indices(finite_element.dofs_per_cell);
    for (const auto& cell : dof_handler.active_cell_iterators()) {
        if (cell->is_artificial()) {
            continue;
        }
        cell->get_dof_indices(dof_indices);
        for (unsigned int local = 0; local < finite_element.dofs_per_cell; ++local) {
            if (finite_element.system_to_component_index(local).first == component) {
                selected.add_index(dof_indices.at(local));
            }
        }
    }
    selected.compress();
    return selected & field_space.locally_relevant_dofs();
}

/** \brief Remove an indexed record after its caller cleared the final store role. */
template<int dim>
void erase_if_unretained(detail::StateStoreControl<dim>& control,
                         const std::shared_ptr<const StateSnapshot<dim>>& snapshot)
{
    if (!snapshot) {
        return;
    }
    const auto found = control.retained.find(snapshot->stamp().snapshot_id.value());
    if (!found->second.pinned) {
        control.retained.erase(found);
    }
}

/** \brief Perform one fixed-size normal-path agreement for a store transition. */
template<int dim>
[[nodiscard]] std::optional<StateErrors>
agree_transition(detail::StateStoreControl<dim>& control, const StoreOperation operation,
                 const StateSnapshotReference reference, StateErrors local_errors)
{
    const unsigned int rank = dealii::Utilities::MPI::this_mpi_process(control.communicator);
    const std::array<std::uint64_t, 6> local{{static_cast<std::uint64_t>(operation), control.id.value(),
                                              reference.store_id.value(), reference.snapshot_id.value(),
                                              control.accepted->stamp().snapshot_id.value(),
                                              static_cast<std::uint64_t>(local_errors.empty())}};
    std::array<std::uint64_t, local.size()> minima{};
    std::array<std::uint64_t, local.size()> maxima{};
    dealii::Utilities::MPI::min(dealii::make_array_view(local), control.communicator, dealii::make_array_view(minima));
    dealii::Utilities::MPI::max(dealii::make_array_view(local), control.communicator, dealii::make_array_view(maxima));
    const bool descriptors_match = std::equal(minima.begin(), std::prev(minima.end()), maxima.begin());
    if (!descriptors_match) {
        add_state_error(local_errors, StateErrorCode::collective_operation_mismatch, rank, reference,
                        std::format("rank {} entered a state-store transition with a descriptor that differs across "
                                    "MPI_COMM_WORLD",
                                    rank));
    }
    if (minima.back() == 0 || !descriptors_match) {
        return collect_state_errors(control.communicator, std::move(local_errors));
    }
    return std::nullopt;
}

} // namespace

template<int dim>
    requires(dim == 2 || dim == 3)
StateStore<dim>::StateStore(const RiftContext* creator_context, const StateStoreId id,
                            std::shared_ptr<const SpaceSnapshot<dim>> space, const RetentionPolicy policy) :
    control_(std::make_shared<detail::StateStoreControl<dim>>())
{
    control_->creator_context = creator_context;
    control_->communicator = MPI_COMM_WORLD;
    control_->id = id;
    control_->space = std::move(space);
    control_->retention = policy;

    auto storage = std::make_unique<detail::StateStorage<dim>>();
    const auto communicator = control_->space->phase_supports().mesh_snapshot().communicator();
    storage->phase_support_fields.reserve(control_->space->phase_support_field_spaces().size());
    for (const auto& field_space : control_->space->phase_support_field_spaces()) {
        storage->phase_support_fields.emplace_back(field_space.locally_owned_dofs(),
                                                   field_space.locally_relevant_dofs(), communicator);
        storage->phase_support_fields.back().update_ghost_values();
    }
    storage->geometry_fields.reserve(control_->space->geometry_field_spaces().size());
    for (const auto& field_space : control_->space->geometry_field_spaces()) {
        storage->geometry_fields.emplace_back(field_space.locally_owned_dofs(), field_space.locally_relevant_dofs(),
                                              communicator);
        storage->geometry_fields.back().update_ghost_values();
    }

    const auto metadata_descriptors = control_->space->canonical_schema().discrete_geometry_metadata();
    storage->discrete_geometry_metadata.reserve(metadata_descriptors.size());
    for (const auto& descriptor : metadata_descriptors) {
        const auto& field_space = control_->space->geometry_field_space(descriptor.geometry_field_group);
        dealii::ComponentMask component_mask(field_space.finite_element().n_components(), false);
        component_mask.set(descriptor.component, true);
        auto owned = dealii::DoFTools::extract_dofs(field_space.dof_handler(), component_mask);
        auto relevant = component_relevant_dofs(field_space, descriptor.component);
        auto ghosts = relevant;
        ghosts.subtract_set(owned);
        ghosts.compress();

        auto implementation = std::make_unique<PhaseLabelMetadata::Impl>();
        implementation->locally_owned_dofs = std::move(owned);
        implementation->ghost_dofs = std::move(ghosts);
        implementation->partitioner = std::make_shared<dealii::Utilities::MPI::NoncontiguousPartitioner>(
            implementation->locally_owned_dofs, implementation->ghost_dofs, communicator);
        implementation->owned_values.assign(implementation->locally_owned_dofs.n_elements(), unassigned_phase_code);
        implementation->ghost_values.assign(implementation->ghost_dofs.n_elements(), unassigned_phase_code);
        implementation->phase_count = control_->space->phase_supports().supports().size();
        PhaseLabelMetadata metadata(std::move(implementation));
        metadata.update_ghost_values();
        storage->discrete_geometry_metadata.push_back(std::move(metadata));
    }
    storage->regional_values.resize(control_->space->state_layout().regional_entries().size());

    const StateSnapshotStamp root_stamp{.space_epoch = control_->space->epoch(),
                                        .store_id = id,
                                        .snapshot_id = StateSnapshotId::from_index(0),
                                        .base_snapshot_id = std::nullopt,
                                        .geometry_revision = GeometryRevision::from_index(0)};
    control_->accepted = std::shared_ptr<const StateSnapshot<dim>>(
        new StateSnapshot<dim>(control_->space, root_stamp, std::move(storage)));
    control_->retained.emplace(0, detail::RetainedSnapshot<dim>{.snapshot = control_->accepted, .pinned = false});
}

template<> StateStore<2>::~StateStore() = default;
template<> StateStore<3>::~StateStore() = default;

template<int dim>
    requires(dim == 2 || dim == 3)
StateStoreId StateStore<dim>::id() const noexcept
{
    return control_->id;
}

template<int dim>
    requires(dim == 2 || dim == 3)
const SpaceSnapshot<dim>& StateStore<dim>::space() const noexcept
{
    return *control_->space;
}

template<int dim>
    requires(dim == 2 || dim == 3)
std::shared_ptr<const StateSnapshot<dim>> StateStore<dim>::accepted() const noexcept
{
    return control_->accepted;
}

template<int dim>
    requires(dim == 2 || dim == 3)
std::shared_ptr<const StateSnapshot<dim>> StateStore<dim>::previous() const noexcept
{
    return control_->previous;
}

template<int dim>
    requires(dim == 2 || dim == 3)
std::shared_ptr<const StateSnapshot<dim>> StateStore<dim>::transient() const noexcept
{
    return control_->transient;
}

template<int dim>
    requires(dim == 2 || dim == 3)
std::expected<std::shared_ptr<const StateSnapshot<dim>>, StateError>
StateStore<dim>::lookup(const StateSnapshotReference reference) const
{
    const unsigned int rank = dealii::Utilities::MPI::this_mpi_process(control_->communicator);
    if (reference.store_id != control_->id) {
        return std::unexpected(StateError{.code = StateErrorCode::foreign_snapshot,
                                          .rank = rank,
                                          .snapshot = reference,
                                          .message = std::format("snapshot reference belongs to store {}, expected {}",
                                                                 reference.store_id.value(), control_->id.value())});
    }
    const auto found = control_->retained.find(reference.snapshot_id.value());
    if (found == control_->retained.end()) {
        return std::unexpected(StateError{.code = StateErrorCode::snapshot_not_retained,
                                          .rank = rank,
                                          .snapshot = reference,
                                          .message = std::format("state store {} no longer indexes snapshot {}",
                                                                 control_->id.value(), reference.snapshot_id.value())});
    }
    return found->second.snapshot;
}

template<int dim>
    requires(dim == 2 || dim == 3)
std::expected<std::shared_ptr<const StateSnapshot<dim>>, StateErrors>
StateStore<dim>::publish(const StateSnapshotReference reference)
{
    const unsigned int rank = dealii::Utilities::MPI::this_mpi_process(control_->communicator);
    StateErrors local_errors;
    std::shared_ptr<const StateSnapshot<dim>> candidate;
    if (reference.store_id != control_->id) {
        add_state_error(local_errors, StateErrorCode::foreign_snapshot, rank, reference,
                        std::format("cannot publish snapshot from store {} through store {}",
                                    reference.store_id.value(), control_->id.value()));
    }
    else if (const auto found = control_->retained.find(reference.snapshot_id.value());
             found == control_->retained.end()) {
        add_state_error(local_errors, StateErrorCode::snapshot_not_retained, rank, reference,
                        std::format("cannot publish unretained snapshot {}", reference.snapshot_id.value()));
    }
    else {
        candidate = found->second.snapshot;
        if (!candidate->stamp().base_snapshot_id.has_value()) {
            add_state_error(local_errors, StateErrorCode::invalid_snapshot_transition, rank, reference,
                            "the initial state root is not a publishable candidate");
        }
        else if (*candidate->stamp().base_snapshot_id != control_->accepted->stamp().snapshot_id) {
            add_state_error(local_errors, StateErrorCode::stale_candidate, rank, reference,
                            std::format("candidate {} was based on snapshot {}, but accepted snapshot is {}",
                                        reference.snapshot_id.value(), candidate->stamp().base_snapshot_id->value(),
                                        control_->accepted->stamp().snapshot_id.value()));
        }
    }
    if (auto errors = agree_transition(*control_, StoreOperation::publish, reference, std::move(local_errors))) {
        return std::unexpected(std::move(*errors));
    }

    const auto former_previous = control_->previous;
    const auto former_accepted = control_->accepted;
    if (control_->transient == candidate) {
        control_->transient.reset();
    }
    control_->previous = former_accepted;
    control_->accepted = candidate;
    erase_if_unretained(*control_, former_previous);
    return control_->accepted;
}

template<int dim>
    requires(dim == 2 || dim == 3)
std::expected<void, StateErrors> StateStore<dim>::pin(const StateSnapshotReference reference)
{
    const unsigned int rank = dealii::Utilities::MPI::this_mpi_process(control_->communicator);
    StateErrors local_errors;
    auto found = control_->retained.end();
    if (reference.store_id != control_->id) {
        add_state_error(local_errors, StateErrorCode::foreign_snapshot, rank, reference,
                        std::format("cannot pin snapshot from store {} through store {}", reference.store_id.value(),
                                    control_->id.value()));
    }
    else {
        found = control_->retained.find(reference.snapshot_id.value());
        if (found == control_->retained.end()) {
            add_state_error(local_errors, StateErrorCode::snapshot_not_retained, rank, reference,
                            std::format("cannot pin unretained snapshot {}", reference.snapshot_id.value()));
        }
        else if (!found->second.pinned && control_->pinned_count >= control_->retention.max_pinned_snapshots) {
            add_state_error(local_errors, StateErrorCode::pin_capacity_exceeded, rank, reference,
                            std::format("pinning snapshot {} would exceed configured capacity {}",
                                        reference.snapshot_id.value(), control_->retention.max_pinned_snapshots));
        }
    }
    if (auto errors = agree_transition(*control_, StoreOperation::pin, reference, std::move(local_errors))) {
        return std::unexpected(std::move(*errors));
    }
    if (!found->second.pinned) {
        found->second.pinned = true;
        ++control_->pinned_count;
    }
    return {};
}

template<int dim>
    requires(dim == 2 || dim == 3)
std::expected<void, StateErrors> StateStore<dim>::unpin(const StateSnapshotReference reference)
{
    const unsigned int rank = dealii::Utilities::MPI::this_mpi_process(control_->communicator);
    StateErrors local_errors;
    auto found = control_->retained.end();
    if (reference.store_id != control_->id) {
        add_state_error(local_errors, StateErrorCode::foreign_snapshot, rank, reference,
                        std::format("cannot unpin snapshot from store {} through store {}", reference.store_id.value(),
                                    control_->id.value()));
    }
    else {
        found = control_->retained.find(reference.snapshot_id.value());
        if (found == control_->retained.end()) {
            add_state_error(local_errors, StateErrorCode::snapshot_not_retained, rank, reference,
                            std::format("cannot unpin unretained snapshot {}", reference.snapshot_id.value()));
        }
    }
    if (auto errors = agree_transition(*control_, StoreOperation::unpin, reference, std::move(local_errors))) {
        return std::unexpected(std::move(*errors));
    }
    if (!found->second.pinned) {
        return {};
    }
    found->second.pinned = false;
    --control_->pinned_count;
    const auto snapshot = found->second.snapshot;
    if (snapshot != control_->accepted && snapshot != control_->previous) {
        const auto former_transient = control_->transient;
        control_->transient = snapshot;
        erase_if_unretained(*control_, former_transient);
    }
    return {};
}

template<int dim>
    requires(dim == 2 || dim == 3)
std::expected<void, StateErrors> StateStore<dim>::discard(const StateSnapshotReference reference)
{
    const unsigned int rank = dealii::Utilities::MPI::this_mpi_process(control_->communicator);
    StateErrors local_errors;
    auto found = control_->retained.end();
    if (reference.store_id != control_->id) {
        add_state_error(local_errors, StateErrorCode::foreign_snapshot, rank, reference,
                        std::format("cannot discard snapshot from store {} through store {}",
                                    reference.store_id.value(), control_->id.value()));
    }
    else {
        found = control_->retained.find(reference.snapshot_id.value());
        if (found == control_->retained.end()) {
            add_state_error(local_errors, StateErrorCode::snapshot_not_retained, rank, reference,
                            std::format("cannot discard unretained snapshot {}", reference.snapshot_id.value()));
        }
        else if (found->second.pinned) {
            add_state_error(local_errors, StateErrorCode::snapshot_pinned, rank, reference,
                            std::format("cannot discard pinned snapshot {}", reference.snapshot_id.value()));
        }
        else if (!found->second.snapshot->stamp().base_snapshot_id.has_value() ||
                 found->second.snapshot == control_->accepted || found->second.snapshot == control_->previous) {
            add_state_error(local_errors, StateErrorCode::invalid_snapshot_transition, rank, reference,
                            std::format("snapshot {} occupies an accepted or previous role and cannot be discarded",
                                        reference.snapshot_id.value()));
        }
    }
    if (auto errors = agree_transition(*control_, StoreOperation::discard, reference, std::move(local_errors))) {
        return std::unexpected(std::move(*errors));
    }
    // Every valid discard target is the current unpinned transient: accepted,
    // previous, pinned, and unindexed snapshots were rejected above.
    control_->transient.reset();
    control_->retained.erase(found);
    return {};
}

template<int dim>
    requires(dim == 2 || dim == 3)
StateStoreResult<dim> RiftContext::create_state_store(std::shared_ptr<const SpaceSnapshot<dim>> space,
                                                      const RetentionPolicy policy)
{
    const unsigned int rank = this_mpi_process();
    StateStoreCreationErrors local_errors;
    const bool has_space = space != nullptr;
    const bool owned_by_context = has_space && space->creator_context_ == this;
    if (!has_space) {
        local_errors.push_back({.code = StateStoreCreationErrorCode::null_space,
                                .rank = rank,
                                .message = std::format("rank {} supplied no finalized space", rank)});
    }
    else if (!owned_by_context) {
        local_errors.push_back(
            {.code = StateStoreCreationErrorCode::foreign_space,
             .rank = rank,
             .message = std::format("rank {} supplied a space finalized by another RiftContext", rank)});
    }
    if (has_space && !space->canonical_schema().discrete_geometry_metadata().empty() &&
        space->phase_supports().supports().size() > unassigned_phase_code) {
        local_errors.push_back({.code = StateStoreCreationErrorCode::phase_label_encoding_overflow,
                                .rank = rank,
                                .message = "canonical phase count exceeds the compact phase-label encoding"});
    }
    if (next_state_store_index_ == std::numeric_limits<std::uint64_t>::max()) {
        local_errors.push_back({.code = StateStoreCreationErrorCode::state_store_id_exhausted,
                                .rank = rank,
                                .message = "RiftContext cannot represent another state-store identity"});
    }

    const std::array<std::uint64_t, 7> local{
        {dim, static_cast<std::uint64_t>(has_space), static_cast<std::uint64_t>(owned_by_context),
         has_space ? space->epoch().value() : 0, has_space ? space->phase_supports().mesh_snapshot().id().value() : 0,
         static_cast<std::uint64_t>(policy.max_pinned_snapshots), next_state_store_index_}};
    std::array<std::uint64_t, local.size()> minima{};
    std::array<std::uint64_t, local.size()> maxima{};
    dealii::Utilities::MPI::min(dealii::make_array_view(local), mpi_communicator(), dealii::make_array_view(minima));
    dealii::Utilities::MPI::max(dealii::make_array_view(local), mpi_communicator(), dealii::make_array_view(maxima));
    const bool agrees = minima == maxima;
    const auto locally_valid = static_cast<unsigned int>(local_errors.empty() && agrees);
    const auto globally_valid = dealii::Utilities::MPI::min(locally_valid, mpi_communicator());
    if (!agrees) {
        local_errors.push_back({.code = StateStoreCreationErrorCode::collective_space_mismatch,
                                .rank = rank,
                                .message = std::format("rank {} supplied state-store construction metadata that "
                                                       "differs across MPI_COMM_WORLD",
                                                       rank)});
    }
    if (globally_valid == 0) {
        return std::unexpected(collect_creation_errors(mpi_communicator(), std::move(local_errors)));
    }

    const auto id = StateStoreId::from_index(next_state_store_index_);
    ++next_state_store_index_;
    return std::unique_ptr<StateStore<dim>>(new StateStore<dim>(this, id, std::move(space), policy));
}

template StateStoreId StateStore<2>::id() const noexcept;
template StateStoreId StateStore<3>::id() const noexcept;
template const SpaceSnapshot<2>& StateStore<2>::space() const noexcept;
template const SpaceSnapshot<3>& StateStore<3>::space() const noexcept;
template std::shared_ptr<const StateSnapshot<2>> StateStore<2>::accepted() const noexcept;
template std::shared_ptr<const StateSnapshot<3>> StateStore<3>::accepted() const noexcept;
template std::shared_ptr<const StateSnapshot<2>> StateStore<2>::previous() const noexcept;
template std::shared_ptr<const StateSnapshot<3>> StateStore<3>::previous() const noexcept;
template std::shared_ptr<const StateSnapshot<2>> StateStore<2>::transient() const noexcept;
template std::shared_ptr<const StateSnapshot<3>> StateStore<3>::transient() const noexcept;
template std::expected<std::shared_ptr<const StateSnapshot<2>>, StateError>
    StateStore<2>::lookup(StateSnapshotReference) const;
template std::expected<std::shared_ptr<const StateSnapshot<3>>, StateError>
    StateStore<3>::lookup(StateSnapshotReference) const;
template std::expected<std::shared_ptr<const StateSnapshot<2>>, StateErrors>
    StateStore<2>::publish(StateSnapshotReference);
template std::expected<std::shared_ptr<const StateSnapshot<3>>, StateErrors>
    StateStore<3>::publish(StateSnapshotReference);
template std::expected<void, StateErrors> StateStore<2>::pin(StateSnapshotReference);
template std::expected<void, StateErrors> StateStore<3>::pin(StateSnapshotReference);
template std::expected<void, StateErrors> StateStore<2>::unpin(StateSnapshotReference);
template std::expected<void, StateErrors> StateStore<3>::unpin(StateSnapshotReference);
template std::expected<void, StateErrors> StateStore<2>::discard(StateSnapshotReference);
template std::expected<void, StateErrors> StateStore<3>::discard(StateSnapshotReference);
template StateStoreResult<2> RiftContext::create_state_store<2>(std::shared_ptr<const SpaceSnapshot<2>>,
                                                                RetentionPolicy);
template StateStoreResult<3> RiftContext::create_state_store<3>(std::shared_ptr<const SpaceSnapshot<3>>,
                                                                RetentionPolicy);

} // namespace rift
