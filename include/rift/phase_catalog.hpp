#pragma once

/**
 * \file
 * \brief Rank-local ownership, registration, and inspection of physical phases.
 */

#include "rift/context.hpp"
#include "rift/phase.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <deal.II/base/exceptions.h>
#include <deal.II/base/mpi.h>
#include <deal.II/base/observer_pointer.h>
#include <deal.II/base/utilities.h>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rift {

/**
 * \brief Owns all rank-local phases associated with one Context.
 * \tparam dim Spatial dimension shared by the owned phase types.
 * \tparam Number Scalar number type shared by the coupled system.
 *
 * Exactly one catalog may claim a Context during that Context's lifetime. The
 * claim remains consumed after this catalog is destroyed. Registration and
 * collective freeze are externally serialized configuration operations.
 * Concurrent const inspection is supported only after freeze() returns.
 */
template<int dim, typename Number> class PhaseCatalog {
public:
    /**
     * \brief Permanently claim a Context for this catalog.
     * \param context Context that must outlive this catalog.
     * \throws dealii::ExceptionBase If any catalog has previously claimed the
     * same Context.
     */
    explicit PhaseCatalog(Context& context) : context_(&context, "PhaseCatalog") { context_->claim_phase_catalog(); }

    /** \brief A catalog has unique ownership and cannot be copied. */
    PhaseCatalog(const PhaseCatalog&) = delete;
    /** \brief Catalog ownership cannot be replaced by copying. */
    PhaseCatalog& operator=(const PhaseCatalog&) = delete;
    /** \brief Preserve the catalog associated with the permanent Context claim. */
    PhaseCatalog(PhaseCatalog&&) = delete;
    /** \brief Catalog ownership cannot be replaced by moving. */
    PhaseCatalog& operator=(PhaseCatalog&&) = delete;
    /** \brief Destroy all phases without releasing the Context claim. */
    ~PhaseCatalog() = default;

    /**
     * \brief Construct and register one concrete physical phase.
     * \tparam ConcretePhase Phase type derived from `Phase<dim, Number>` and
     * constructible from `PhaseDescriptor` followed by `Args...`.
     * \tparam Args Model-specific constructor argument types.
     * \param name Application phase name, stored without normalization.
     * \param args Model-specific constructor arguments.
     * \return Dense Context-local identity assigned to the new phase.
     * \throws dealii::ExceptionBase If the catalog is frozen, if `name` is
     * empty after trimming whitespace, or if `name` exactly duplicates an
     * existing phase name. Exceptions from phase construction and allocation
     * propagate unchanged.
     *
     * Failure leaves the catalog unchanged. The concrete phase supplies
     * `static constexpr` model_identifier() and discretization_identifier()
     * functions returning `std::string_view`.
     */
    template<typename ConcretePhase, typename... Args>
        requires std::derived_from<ConcretePhase, Phase<dim, Number>> &&
                 std::constructible_from<ConcretePhase, PhaseDescriptor, Args...> && requires {
                     { ConcretePhase::model_identifier() } noexcept -> std::same_as<std::string_view>;
                     { ConcretePhase::discretization_identifier() } noexcept -> std::same_as<std::string_view>;
                 }
    PhaseId emplace(std::string name, Args&&... args)
    {
        const PhaseId id = validate_registration(name);
        PhaseDescriptor descriptor(id, std::move(name), ModelId(std::string(ConcretePhase::model_identifier())),
                                   DiscretizationId(std::string(ConcretePhase::discretization_identifier())));
        auto phase = std::make_unique<ConcretePhase>(std::move(descriptor), std::forward<Args>(args)...);
        phases_.push_back(std::move(phase));
        return id;
    }

    /**
     * \brief Collectively verify descriptor agreement and prevent registration.
     *
     * Every rank in the Context communicator must call this operation. The
     * ordered phase IDs, names, model IDs, and discretization IDs are compared
     * against rank zero. Repeated collective calls after success are
     * idempotent.
     *
     * \throws dealii::ExceptionBase If the catalogs differ or if every catalog
     * is empty. Every catalog remains open and unchanged after either failure.
     */
    void freeze()
    {
        if (frozen_) {
            return;
        }

        const auto signatures = dealii::Utilities::MPI::all_gather(context_->mpi_comm(), make_local_signature());
        const std::string mismatch = first_mismatch(signatures);
        AssertThrow(mismatch.empty(), dealii::ExcMessage(mismatch));
        AssertThrow(!signatures.front().empty(),
                    dealii::ExcMessage("Cannot freeze an empty PhaseCatalog; register at least one phase."));
        frozen_ = true;
    }

    /** \brief Return the number of successfully registered phases. */
    [[nodiscard]] std::size_t size() const noexcept { return phases_.size(); }

    /** \brief Return whether no phases have been registered. */
    [[nodiscard]] bool empty() const noexcept { return phases_.empty(); }

    /** \brief Return whether local registration has been frozen. */
    [[nodiscard]] bool is_frozen() const noexcept { return frozen_; }

    /**
     * \brief Borrow a registered phase by its Context-local identity.
     * \param id Identity assigned by this Context's catalog.
     * \return Immutable phase reference valid while this catalog remains alive.
     * \throws dealii::ExceptionBase If the wrapped index is outside this catalog.
     */
    [[nodiscard]] const Phase<dim, Number>& at(const PhaseId id) const
    {
        AssertThrow(id.index() < phases_.size(), dealii::ExcMessage("The PhaseId is outside this PhaseCatalog."));
        return *phases_.at(id.index());
    }

private:
    /** \brief Validate common preconditions and return the next candidate ID. */
    [[nodiscard]] PhaseId validate_registration(const std::string& name) const
    {
        AssertThrow(!frozen_, dealii::ExcMessage("Cannot register a phase after the PhaseCatalog is frozen."));
        AssertThrow(!dealii::Utilities::trim(name).empty(),
                    dealii::ExcMessage("A phase name must contain at least one non-whitespace character."));

        const bool name_exists =
            std::ranges::any_of(phases_, [&name](const auto& phase) { return phase->descriptor().name() == name; });
        AssertThrow(!name_exists, dealii::ExcMessage("A phase named '" + name + "' is already registered."));
        return PhaseId(phases_.size());
    }

    /** \brief Serializable values for ID, name, model, and discretization. */
    using DescriptorSignature = std::array<std::string, 4>;
    /** \brief Ordered semantic signature of one rank's complete catalog. */
    using CatalogSignature = std::vector<DescriptorSignature>;

    /** \brief Construct the semantic signature exchanged during freeze(). */
    [[nodiscard]] CatalogSignature make_local_signature() const
    {
        CatalogSignature signature;
        signature.reserve(phases_.size());
        for (const auto& phase : phases_) {
            const auto& descriptor = phase->descriptor();
            signature.push_back(DescriptorSignature{
                {std::to_string(descriptor.id().index()), std::string(descriptor.name()),
                 std::string(descriptor.model_id().value()), std::string(descriptor.discretization_id().value())}});
        }
        return signature;
    }

    /** \brief Return the deterministic first mismatch, or an empty string. */
    [[nodiscard]] static std::string first_mismatch(const std::vector<CatalogSignature>& signatures)
    {
        const auto& reference = signatures.front();
        constexpr std::array<std::string_view, 4> field_names{{"id", "name", "model id", "discretization id"}};

        for (std::size_t rank = 1; rank < signatures.size(); ++rank) {
            const auto& candidate = signatures.at(rank);
            if (candidate.size() != reference.size()) {
                return "PhaseCatalog mismatch at rank " + std::to_string(rank) +
                       ": phase count differs (rank 0: " + std::to_string(reference.size()) + ", rank " +
                       std::to_string(rank) + ": " + std::to_string(candidate.size()) + ").";
            }

            for (std::size_t phase = 0; phase < reference.size(); ++phase) {
                const auto& reference_phase = reference.at(phase);
                const auto& candidate_phase = candidate.at(phase);
                for (std::size_t field = 0; field < field_names.size(); ++field) {
                    const auto& reference_value = reference_phase.at(field);
                    const auto& candidate_value = candidate_phase.at(field);
                    if (candidate_value != reference_value) {
                        std::ostringstream message;
                        message << "PhaseCatalog mismatch at rank " << rank << ", phase " << phase << ", field '"
                                << field_names.at(field) << "' (rank 0: '" << reference_value << "', rank " << rank
                                << ": '" << candidate_value << "').";
                        return std::move(message).str();
                    }
                }
            }
        }
        return {};
    }

    /** \brief Context whose permanent catalog claim this object owns. */
    dealii::ObserverPointer<Context> context_;
    /** \brief Heterogeneous phases in dense identity order. */
    std::vector<std::unique_ptr<Phase<dim, Number>>> phases_;
    /** \brief Monotonic local registration state. */
    bool frozen_{false};
};

} // namespace rift
