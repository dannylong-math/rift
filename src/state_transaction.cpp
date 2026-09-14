/**
 * \file
 * \brief Move-only state mutation and collective immutable sealing.
 */

#include "state_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <boost/serialization/string.hpp> // NOLINT(misc-include-cleaner): instantiates Boost archive support.
#include <boost/serialization/vector.hpp> // NOLINT(misc-include-cleaner): instantiates all_gather payload support.
#include <cstddef>
#include <cstdint>
#include <deal.II/base/array_view.h>
#include <deal.II/base/mpi.h>
#include <deal.II/base/types.h>
#include <deal.II/lac/la_parallel_vector.h>
#include <expected>
#include <format>
#include <iterator>
#include <limits>
#include <memory>
#include <mpi.h>
#include <optional>
#include <rift/space_snapshot.hpp>
#include <rift/state_snapshot.hpp>
#include <rift/state_store.hpp>
#include <rift/state_transaction.hpp>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace rift {

namespace {

/** \brief Serialization-friendly state diagnostic. */
struct StateErrorWire {
    /** \brief Encoded state-error classification. */
    std::uint8_t code = 0;
    /** \brief Rank that observed the error. */
    unsigned int rank = 0;
    /** \brief Human-readable error detail. */
    std::string message;

    /** \brief Serialize one state diagnostic for failure-only exchange. */
    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & code;
        archive & rank;
        archive & message;
    }
};

/** \brief Carry one exact `(regional entry, binary64 bits)` update. */
using RegionalUpdateWire = std::array<std::uint64_t, 2>;

/** \brief Add one typed local state diagnostic. */
void add_error(StateErrors& errors, const StateErrorCode code, const unsigned int rank, std::string message)
{
    errors.push_back({.code = code, .rank = rank, .snapshot = std::nullopt, .message = std::move(message)});
}

/** \brief Gather and deterministically order failure-only diagnostics. */
[[nodiscard]] StateErrors collect_errors(const MPI_Comm communicator, StateErrors local_errors)
{
    std::vector<StateErrorWire> local_wire;
    local_wire.reserve(local_errors.size());
    std::ranges::transform(local_errors, std::back_inserter(local_wire), [](auto& error) {
        return StateErrorWire{
            .code = static_cast<std::uint8_t>(error.code), .rank = error.rank, .message = std::move(error.message)};
    });
    const auto gathered = dealii::Utilities::MPI::all_gather(communicator, local_wire);
    StateErrors errors;
    for (const auto& rank_errors : gathered) {
        for (auto error : rank_errors) {
            errors.push_back({.code = static_cast<StateErrorCode>(error.code),
                              .rank = error.rank,
                              .snapshot = std::nullopt,
                              .message = std::move(error.message)});
        }
    }
    std::ranges::sort(errors, [](const auto& left, const auto& right) {
        return std::tuple{left.rank, left.code, left.message} < std::tuple{right.rank, right.code, right.message};
    });
    return errors;
}

/** \brief Compare authoritative continuous values by exact object representation. */
[[nodiscard]] bool exact_owned_values_differ(const dealii::LinearAlgebra::distributed::Vector<double>& left,
                                             const dealii::LinearAlgebra::distributed::Vector<double>& right)
{
    // Candidates are created only by copying their immutable base storage, so
    // both vectors necessarily retain the same locally owned partition.
    for (dealii::types::global_dof_index local = 0; local < left.locally_owned_size(); ++local) {
        if (std::bit_cast<std::uint64_t>(left.local_element(local)) !=
            std::bit_cast<std::uint64_t>(right.local_element(local))) {
            return true;
        }
    }
    return false;
}

/** \brief Advance a never-reused finite counter after reserving its current value. */
[[nodiscard]] bool consume_counter(std::uint64_t& next) noexcept
{
    if (next == std::numeric_limits<std::uint64_t>::max()) {
        return true;
    }
    ++next;
    return false;
}

} // namespace

/** \brief Retain collective transaction identity after store destruction. */
template<int dim>
    requires(dim == 2 || dim == 3)
struct StateTransaction<dim>::Tombstone {
    /** \brief Borrowed communicator retained for coherent inactive/expired errors. */
    MPI_Comm communicator;
    /** \brief Store identity expected by every participating rank. */
    StateStoreId store_id;
    /** \brief Transaction identity expected by every participating rank. */
    StateTransactionId transaction_id;
    /** \brief Accepted root cloned when the transaction began. */
    StateSnapshotId base_snapshot_id;
};

template<int dim>
    requires(dim == 2 || dim == 3)
StateTransaction<dim>::StateTransaction(std::weak_ptr<detail::StateStoreControl<dim>> store,
                                        std::shared_ptr<const StateSnapshot<dim>> base, const StateTransactionId id,
                                        std::unique_ptr<detail::StateStorage<dim>> candidate) :
    tombstone_(std::make_shared<Tombstone>(Tombstone{.communicator = MPI_COMM_WORLD,
                                                     .store_id = base->stamp().store_id,
                                                     .transaction_id = id,
                                                     .base_snapshot_id = base->stamp().snapshot_id})),
    store_(std::move(store)),
    base_(std::move(base)),
    candidate_(std::move(candidate)),
    regional_updates_(candidate_->regional_values.size())
{
}

template<int dim>
    requires(dim == 2 || dim == 3)
StateTransaction<dim>::~StateTransaction()
{
    abandon();
}

template<int dim>
    requires(dim == 2 || dim == 3)
StateTransaction<dim>::StateTransaction(StateTransaction&& other) noexcept :
    tombstone_(other.tombstone_), // NOLINT(performance-move-constructor-init): moved-from identity remains queryable.
    store_(other.store_),
    base_(std::move(other.base_)),
    candidate_(std::move(other.candidate_)),
    regional_updates_(std::move(other.regional_updates_))
{
    other.regional_updates_.clear();
}

template<int dim>
    requires(dim == 2 || dim == 3)
bool StateTransaction<dim>::active() const noexcept
{
    return candidate_ != nullptr;
}

template<int dim>
    requires(dim == 2 || dim == 3)
StateTransactionId StateTransaction<dim>::id() const noexcept
{
    return tombstone_->transaction_id;
}

template<int dim>
    requires(dim == 2 || dim == 3)
const StateSnapshot<dim>& StateTransaction<dim>::base() const
{
    if (!active()) {
        throw std::logic_error("inactive state transaction has no accessible base");
    }
    if (store_.expired()) {
        throw std::logic_error("state transaction's store has expired");
    }
    return *base_;
}

template<int dim>
    requires(dim == 2 || dim == 3)
dealii::LinearAlgebra::distributed::Vector<double>&
StateTransaction<dim>::phase_support_field(const PhaseSupportFieldReference reference)
{
    static_cast<void>(base());
    if (reference.space_epoch != base_->stamp().space_epoch) {
        throw std::invalid_argument(std::format("phase-support field reference belongs to space epoch {}, expected {}",
                                                reference.space_epoch.value(), base_->stamp().space_epoch.value()));
    }
    if (reference.field_group.value() >= candidate_->phase_support_fields.size()) {
        throw std::out_of_range(std::format("phase-support field ID {} is outside [0, {})",
                                            reference.field_group.value(), candidate_->phase_support_fields.size()));
    }
    return candidate_->phase_support_fields.at(reference.field_group.value());
}

template<int dim>
    requires(dim == 2 || dim == 3)
dealii::LinearAlgebra::distributed::Vector<double>&
StateTransaction<dim>::geometry_field(const GeometryFieldReference reference)
{
    static_cast<void>(base());
    if (reference.space_epoch != base_->stamp().space_epoch) {
        throw std::invalid_argument(std::format("geometry field reference belongs to space epoch {}, expected {}",
                                                reference.space_epoch.value(), base_->stamp().space_epoch.value()));
    }
    if (reference.field_group.value() >= candidate_->geometry_fields.size()) {
        throw std::out_of_range(std::format("geometry field ID {} is outside [0, {})", reference.field_group.value(),
                                            candidate_->geometry_fields.size()));
    }
    return candidate_->geometry_fields.at(reference.field_group.value());
}

template<int dim>
    requires(dim == 2 || dim == 3)
MutablePhaseLabelMetadataView
StateTransaction<dim>::discrete_geometry_metadata(const DiscreteGeometryMetadataReference reference)
{
    static_cast<void>(base());
    if (reference.space_epoch != base_->stamp().space_epoch) {
        throw std::invalid_argument(
            std::format("discrete geometry-metadata reference belongs to space epoch {}, expected {}",
                        reference.space_epoch.value(), base_->stamp().space_epoch.value()));
    }
    if (reference.metadata.value() >= candidate_->discrete_geometry_metadata.size()) {
        throw std::out_of_range(std::format("discrete geometry-metadata ID {} is outside [0, {})",
                                            reference.metadata.value(), candidate_->discrete_geometry_metadata.size()));
    }
    return MutablePhaseLabelMetadataView(candidate_->discrete_geometry_metadata.at(reference.metadata.value()));
}

template<int dim>
    requires(dim == 2 || dim == 3)
std::expected<void, StateError> StateTransaction<dim>::set_regional(const RegionalEntryReference reference,
                                                                    const double value)
{
    static_cast<void>(base());
    if (reference.space_epoch != base_->stamp().space_epoch) {
        throw std::invalid_argument(std::format("regional reference belongs to space epoch {}, expected {}",
                                                reference.space_epoch.value(), base_->stamp().space_epoch.value()));
    }
    const auto& entry = base_->space().state_layout().regional_entry(reference.regional_entry);
    const unsigned int rank = dealii::Utilities::MPI::this_mpi_process(tombstone_->communicator);
    if (rank != entry.owner_rank) {
        return std::unexpected(
            StateError{.code = StateErrorCode::not_regional_owner,
                       .rank = rank,
                       .snapshot = base_->reference(),
                       .message = std::format("rank {} cannot set regional entry {}; owner is rank {}", rank,
                                              entry.id.value(), entry.owner_rank)});
    }
    regional_updates_.at(reference.regional_entry.value()) = std::bit_cast<std::uint64_t>(value);
    return {};
}

template<int dim>
    requires(dim == 2 || dim == 3)
StateSealResult<dim>
StateTransaction<dim>::seal() // NOLINT(readability-function-cognitive-complexity): one atomic state transition.
{
    const unsigned int rank = dealii::Utilities::MPI::this_mpi_process(tombstone_->communicator);
    auto control = store_.lock();
    StateErrors local_errors;
    if (!control) {
        add_error(local_errors, StateErrorCode::expired_store, rank,
                  std::format("transaction {} cannot seal because its state store expired",
                              tombstone_->transaction_id.value()));
    }
    if (!active()) {
        add_error(local_errors, StateErrorCode::inactive_transaction, rank,
                  std::format("transaction {} is inactive", tombstone_->transaction_id.value()));
    }

    bool local_geometry_changed = false;
    if (control && active()) {
        for (std::size_t index = 0; index < candidate_->geometry_fields.size(); ++index) {
            if (exact_owned_values_differ(candidate_->geometry_fields.at(index),
                                          base_->storage_->geometry_fields.at(index))) {
                local_geometry_changed = true;
                break;
            }
        }
        if (!local_geometry_changed) {
            for (std::size_t index = 0; index < candidate_->discrete_geometry_metadata.size(); ++index) {
                if (!std::ranges::equal(candidate_->discrete_geometry_metadata.at(index).owned_codes(),
                                        base_->storage_->discrete_geometry_metadata.at(index).owned_codes())) {
                    local_geometry_changed = true;
                    break;
                }
            }
        }
    }

    const std::array<std::uint64_t, 11> local{
        {tombstone_->store_id.value(), tombstone_->transaction_id.value(), tombstone_->base_snapshot_id.value(),
         static_cast<std::uint64_t>(active()), static_cast<std::uint64_t>(control != nullptr),
         control ? control->next_snapshot_id : 0, control ? control->next_geometry_revision : 0,
         control ? static_cast<std::uint64_t>(control->snapshot_ids_exhausted) : 0,
         control ? static_cast<std::uint64_t>(control->geometry_revisions_exhausted) : 0,
         static_cast<std::uint64_t>(local_errors.empty()), static_cast<std::uint64_t>(local_geometry_changed)}};
    std::array<std::uint64_t, local.size()> minima{};
    std::array<std::uint64_t, local.size()> maxima{};
    dealii::Utilities::MPI::min(dealii::make_array_view(local), tombstone_->communicator,
                                dealii::make_array_view(minima));
    dealii::Utilities::MPI::max(dealii::make_array_view(local), tombstone_->communicator,
                                dealii::make_array_view(maxima));

    const bool descriptors_match = std::equal(minima.begin(), std::prev(minima.end(), 2), maxima.begin());
    const bool globally_changed = maxima.back() != 0;
    if (!descriptors_match) {
        add_error(local_errors, StateErrorCode::collective_operation_mismatch, rank,
                  std::format("rank {} entered seal with a transaction descriptor that differs across "
                              "MPI_COMM_WORLD",
                              rank));
    }
    if (control && active() && control->snapshot_ids_exhausted) {
        add_error(local_errors, StateErrorCode::state_snapshot_id_exhausted, rank,
                  "state snapshot identity sequence is exhausted");
    }
    if (control && active() && globally_changed && control->geometry_revisions_exhausted) {
        add_error(local_errors, StateErrorCode::geometry_revision_exhausted, rank,
                  "changed geometry cannot receive another revision");
    }
    const auto locally_valid = static_cast<unsigned int>(local_errors.empty());
    const auto globally_valid = dealii::Utilities::MPI::min(locally_valid, tombstone_->communicator);
    if (globally_valid == 0) {
        return std::unexpected(collect_errors(tombstone_->communicator, std::move(local_errors)));
    }

    std::vector<RegionalUpdateWire> local_updates;
    for (std::size_t index = 0; index < regional_updates_.size(); ++index) {
        const auto& update = regional_updates_.at(index);
        if (update.has_value()) {
            local_updates.push_back({{static_cast<std::uint64_t>(index), update.value_or(std::uint64_t{0})}});
        }
    }
    if (!regional_updates_.empty()) {
        const auto gathered_updates = dealii::Utilities::MPI::all_gather(tombstone_->communicator, local_updates);
        for (const auto& updates : gathered_updates) {
            for (const auto& update : updates) {
                candidate_->regional_values.at(update.at(0)) = std::bit_cast<double>(update.at(1));
            }
        }
    }

    for (auto& vector : candidate_->phase_support_fields) {
        vector.zero_out_ghost_values();
        vector.update_ghost_values();
    }
    for (auto& vector : candidate_->geometry_fields) {
        vector.zero_out_ghost_values();
        vector.update_ghost_values();
    }
    for (auto& metadata : candidate_->discrete_geometry_metadata) {
        metadata.update_ghost_values();
    }

    const auto snapshot_id = StateSnapshotId::from_index(control->next_snapshot_id);
    const auto revision = globally_changed ? GeometryRevision::from_index(control->next_geometry_revision)
                                           : base_->stamp().geometry_revision;
    const StateSnapshotStamp stamp{.space_epoch = base_->stamp().space_epoch,
                                   .store_id = control->id,
                                   .snapshot_id = snapshot_id,
                                   .base_snapshot_id = base_->stamp().snapshot_id,
                                   .geometry_revision = revision};
    auto snapshot =
        std::shared_ptr<const StateSnapshot<dim>>(new StateSnapshot<dim>(control->space, stamp, std::move(candidate_)));
    control->snapshot_ids_exhausted = consume_counter(control->next_snapshot_id);
    if (globally_changed) {
        control->geometry_revisions_exhausted = consume_counter(control->next_geometry_revision);
    }

    const auto former_transient = control->transient;
    control->transient = snapshot;
    control->retained.emplace(snapshot_id.value(),
                              detail::RetainedSnapshot<dim>{.snapshot = snapshot, .pinned = false});
    // The transient role is disjoint from accepted and previous. Publication
    // clears it before either of those roles can be assigned.
    if (former_transient) {
        const auto found = control->retained.find(former_transient->stamp().snapshot_id.value());
        if (!found->second.pinned) {
            control->retained.erase(found);
        }
    }

    base_.reset();
    regional_updates_.clear();
    return snapshot;
}

template<int dim>
    requires(dim == 2 || dim == 3)
void StateTransaction<dim>::abandon() noexcept
{
    candidate_.reset();
    base_.reset();
    regional_updates_.clear();
}

template<int dim>
    requires(dim == 2 || dim == 3)
StateTransactionResult<dim> StateStore<dim>::begin_transaction()
{
    const unsigned int rank = dealii::Utilities::MPI::this_mpi_process(control_->communicator);
    StateErrors local_errors;
    if (control_->transaction_ids_exhausted) {
        add_error(local_errors, StateErrorCode::transaction_id_exhausted, rank,
                  "state transaction identity sequence is exhausted");
    }
    const std::array<std::uint64_t, 5> local{{control_->id.value(), control_->accepted->stamp().snapshot_id.value(),
                                              control_->next_transaction_id,
                                              static_cast<std::uint64_t>(control_->transaction_ids_exhausted),
                                              static_cast<std::uint64_t>(local_errors.empty())}};
    std::array<std::uint64_t, local.size()> minima{};
    std::array<std::uint64_t, local.size()> maxima{};
    dealii::Utilities::MPI::min(dealii::make_array_view(local), control_->communicator,
                                dealii::make_array_view(minima));
    dealii::Utilities::MPI::max(dealii::make_array_view(local), control_->communicator,
                                dealii::make_array_view(maxima));
    const bool descriptors_match = std::equal(minima.begin(), std::prev(minima.end()), maxima.begin());
    if (!descriptors_match) {
        add_error(local_errors, StateErrorCode::collective_operation_mismatch, rank,
                  std::format("rank {} entered begin_transaction with different store state", rank));
    }
    if (minima.back() == 0 || !descriptors_match) {
        return std::unexpected(collect_errors(control_->communicator, std::move(local_errors)));
    }

    auto candidate = std::make_unique<detail::StateStorage<dim>>(*control_->accepted->storage_);
    for (auto& vector : candidate->phase_support_fields) {
        vector.zero_out_ghost_values();
    }
    for (auto& vector : candidate->geometry_fields) {
        vector.zero_out_ghost_values();
    }
    const auto transaction_id = StateTransactionId::from_index(control_->next_transaction_id);
    control_->transaction_ids_exhausted = consume_counter(control_->next_transaction_id);
    return StateTransaction<dim>(control_, control_->accepted, transaction_id, std::move(candidate));
}

template class StateTransaction<2>;
template class StateTransaction<3>;

template StateTransactionResult<2> StateStore<2>::begin_transaction();
template StateTransactionResult<3> StateStore<3>::begin_transaction();

} // namespace rift
