#pragma once

/**
 * \file
 * \brief Rank-local ownership, registration, and inspection of physical phases.
 */

#include "rift/context.hpp"
#include "rift/phase.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <deal.II/base/exceptions.h>
#include <deal.II/base/observer_pointer.h>
#include <deal.II/base/utilities.h>
#include <memory>
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
 * local freeze are externally serialized configuration operations. Concurrent
 * const inspection is supported only after freeze_local() returns.
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
        AssertThrow(!frozen_, dealii::ExcMessage("Cannot register a phase after the PhaseCatalog is frozen."));
        AssertThrow(!dealii::Utilities::trim(name).empty(),
                    dealii::ExcMessage("A phase name must contain at least one non-whitespace character."));

        const bool name_exists =
            std::ranges::any_of(phases_, [&name](const auto& phase) { return phase->descriptor().name() == name; });
        AssertThrow(!name_exists, dealii::ExcMessage("A phase named '" + name + "' is already registered."));

        const PhaseId id(phases_.size());
        PhaseDescriptor descriptor(id, std::move(name), ModelId(std::string(ConcretePhase::model_identifier())),
                                   DiscretizationId(std::string(ConcretePhase::discretization_identifier())));
        auto phase = std::make_unique<ConcretePhase>(std::move(descriptor), std::forward<Args>(args)...);
        phases_.push_back(std::move(phase));
        return id;
    }

    /** \brief Idempotently prevent any further local registration. */
    void freeze_local() noexcept { frozen_ = true; }

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
    /** \brief Context whose permanent catalog claim this object owns. */
    dealii::ObserverPointer<Context> context_;
    /** \brief Heterogeneous phases in dense identity order. */
    std::vector<std::unique_ptr<Phase<dim, Number>>> phases_;
    /** \brief Monotonic local registration state. */
    bool frozen_{false};
};

} // namespace rift
