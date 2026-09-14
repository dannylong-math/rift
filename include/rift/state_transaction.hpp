#pragma once

/**
 * \file
 * \brief Move-only mutation and collective sealing of immutable state.
 */

#include <expected>
#include <memory>
#include <rift/state_snapshot.hpp>
#include <vector>

namespace rift {

namespace detail {
template<int dim>
    requires(dim == 2 || dim == 3)
struct StateStoreControl;
}

/** \brief Result of collectively sealing one state transaction. */
template<int dim>
    requires(dim == 2 || dim == 3)
using StateSealResult = std::expected<std::shared_ptr<const StateSnapshot<dim>>, StateErrors>;

/**
 * \brief Own isolated mutable candidate state derived from one accepted root.
 *
 * The transaction is move-constructible but not assignable. Destruction and
 * `abandon()` are local and never communicate; `seal()` is collective.
 */
template<int dim>
    requires(dim == 2 || dim == 3)
class StateTransaction {
public:
    /** \brief Abandon any active candidate locally without MPI communication. */
    ~StateTransaction();
    StateTransaction(const StateTransaction&) = delete;
    StateTransaction& operator=(const StateTransaction&) = delete;
    /** \brief Transfer sole mutable authority and deactivate the source. */
    StateTransaction(StateTransaction&& other) noexcept;
    /** \brief Assignment is disabled to avoid silently discarding a candidate. */
    StateTransaction& operator=(StateTransaction&&) = delete;

    /** \brief Report whether this object still owns mutable candidate storage. */
    [[nodiscard]] bool active() const noexcept;
    /** \brief Return this store-local transaction identity. */
    [[nodiscard]] StateTransactionId id() const noexcept;
    /** \brief Return the immutable accepted snapshot cloned at creation. */
    [[nodiscard]] const StateSnapshot<dim>& base() const;

    /** \brief Mutate one support-restricted continuous candidate vector. */
    [[nodiscard]] dealii::LinearAlgebra::distributed::Vector<double>&
    phase_support_field(PhaseSupportFieldReference reference);
    /** \brief Mutate one continuous geometry candidate vector. */
    [[nodiscard]] dealii::LinearAlgebra::distributed::Vector<double>& geometry_field(GeometryFieldReference reference);
    /** \brief Mutate owned entries of one compact phase-label candidate block. */
    [[nodiscard]] MutablePhaseLabelMetadataView discrete_geometry_metadata(DiscreteGeometryMetadataReference reference);

    /**
     * \brief Stage one owner-authoritative regional scalar update locally.
     * \return success or `not_regional_owner`; invalid references throw.
     */
    [[nodiscard]] std::expected<void, StateError> set_regional(RegionalEntryReference reference, double value);

    /** \brief Collectively seal candidate storage as an immutable transient. */
    [[nodiscard]] StateSealResult<dim> seal();

    /** \brief Idempotently discard candidate storage without communication. */
    void abandon() noexcept;

private:
    friend class StateStore<dim>;

    /** \brief Adopt a cloned candidate and weak publication authority. */
    StateTransaction(std::weak_ptr<detail::StateStoreControl<dim>> store,
                     std::shared_ptr<const StateSnapshot<dim>> base, StateTransactionId id,
                     std::unique_ptr<detail::StateStorage<dim>> candidate);

    struct Tombstone;
    /** \brief Retain collective identity even after the store expires. */
    std::shared_ptr<const Tombstone> tombstone_;
    /** \brief Weak mutable authority; transactions never extend store lifetime. */
    std::weak_ptr<detail::StateStoreControl<dim>> store_;
    /** \brief Retained immutable accepted root. */
    std::shared_ptr<const StateSnapshot<dim>> base_;
    /** \brief Sole mutable candidate storage while active. */
    std::unique_ptr<detail::StateStorage<dim>> candidate_;
    /** \brief Exact owner-staged regional binary64 representations by entry ID. */
    std::vector<std::optional<std::uint64_t>> regional_updates_;
};

/** \brief Result of collectively beginning one transaction. */
template<int dim>
    requires(dim == 2 || dim == 3)
using StateTransactionResult = std::expected<StateTransaction<dim>, StateErrors>;

} // namespace rift
