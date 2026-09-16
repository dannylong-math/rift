#pragma once

/**
 * \file
 * \brief Phase identity, immutable metadata, and the phase type-erasure base.
 */

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace rift {

template<int dim, typename Number> class PhaseCatalog;

/**
 * \brief Identify one registered phase within a Context.
 *
 * PhaseCatalog assigns IDs in registration order and returns them from
 * emplace(). Use an ID to retrieve the same phase from that catalog. IDs compare
 * by their zero-based indices, but an equal index from another Context does not
 * identify the same phase. Application code cannot construct IDs directly.
 *
 * \code{.cpp}
 * const rift::PhaseId liquid =
 *     phases.emplace<FluidPhase>("liquid", viscosity);
 * const auto& phase = phases.at(liquid);
 * \endcode
 */
class PhaseId {
public:
    /** \brief A phase ID exists only after successful catalog registration. */
    PhaseId() = delete;

    /**
     * \brief Return the dense zero-based phase index.
     * \return Index assigned by the owning catalog.
     */
    [[nodiscard]] constexpr std::size_t index() const noexcept { return index_; }

    /** \brief Compare phase IDs by their dense indices. */
    auto operator<=>(const PhaseId&) const = default;

private:
    /** \brief Construct an ID on behalf of the sole catalog for a Context. */
    explicit constexpr PhaseId(const std::size_t index) noexcept : index_(index) {}

    template<int dim, typename Number> friend class PhaseCatalog;

    /** \brief Dense zero-based index in the owning catalog. */
    std::size_t index_;
};

/**
 * \brief Name the governing bulk model implemented by a concrete phase type.
 *
 * A PhaseCatalog records this owning value in each PhaseDescriptor. Concrete
 * phase types normally supply the source string through model_identifier();
 * callers inspect value() when selecting or reporting model behavior.
 *
 * \code{.cpp}
 * const std::string_view model = phase.descriptor().model_id().value();
 * \endcode
 */
class ModelId {
public:
    /**
     * \brief Construct an owning model identifier.
     * \param value Canonical model identifier supplied by a concrete phase.
     */
    explicit ModelId(std::string value) : value_(std::move(value)) {}

    /**
     * \brief Borrow the canonical identifier.
     * \return View valid while this object is alive and unmodified.
     */
    [[nodiscard]] std::string_view value() const noexcept { return value_; }

    /** \brief Compare model identifiers by their canonical strings. */
    auto operator<=>(const ModelId&) const = default;

private:
    /** \brief Owned canonical identifier. */
    std::string value_;
};

/**
 * \brief Name the compiled spatial discretization used by a concrete phase type.
 *
 * This identifier distinguishes alternative implementations of the same bulk
 * model. A PhaseCatalog stores the owning value in each PhaseDescriptor, and
 * callers may inspect value() for diagnostics and configuration checks.
 *
 * \code{.cpp}
 * const std::string_view discretization =
 *     phase.descriptor().discretization_id().value();
 * \endcode
 */
class DiscretizationId {
public:
    /**
     * \brief Construct an owning discretization identifier.
     * \param value Canonical identifier supplied by a concrete phase.
     */
    explicit DiscretizationId(std::string value) : value_(std::move(value)) {}

    /**
     * \brief Borrow the canonical identifier.
     * \return View valid while this object is alive and unmodified.
     */
    [[nodiscard]] std::string_view value() const noexcept { return value_; }

    /** \brief Compare discretization identifiers by their canonical strings. */
    auto operator<=>(const DiscretizationId&) const = default;

private:
    /** \brief Owned canonical identifier. */
    std::string value_;
};

/**
 * \brief Describe the identity and implementation of one registered phase.
 *
 * The PhaseCatalog constructs this value while registering a phase. It combines
 * the Context-local PhaseId and application-supplied name with the concrete
 * type's model and discretization identifiers. Consumers normally inspect a
 * descriptor through Phase::descriptor(); they may copy it, but its fields
 * cannot be replaced after construction.
 *
 * \code{.cpp}
 * const rift::PhaseDescriptor& descriptor = phases.at(liquid).descriptor();
 * context.pcout() << descriptor.id().index() << ": " << descriptor.name()
 *                 << '\n';
 * \endcode
 */
class PhaseDescriptor {
public:
    /** \brief Copy immutable metadata into an independent value. */
    PhaseDescriptor(const PhaseDescriptor&) = default;
    /** \brief Move immutable metadata into a new value. */
    PhaseDescriptor(PhaseDescriptor&&) noexcept = default;
    /** \brief Metadata fields cannot be replaced after construction. */
    PhaseDescriptor& operator=(const PhaseDescriptor&) = delete;
    /** \brief Metadata fields cannot be replaced after construction. */
    PhaseDescriptor& operator=(PhaseDescriptor&&) = delete;
    /** \brief Destroy the owned strings and metadata values. */
    ~PhaseDescriptor() = default;

    /**
     * \brief Return the Context-local phase identity.
     * \return ID assigned in catalog registration order.
     */
    [[nodiscard]] constexpr PhaseId id() const noexcept { return id_; }
    /**
     * \brief Borrow the application-supplied phase name.
     * \return Exact, unnormalized name supplied to PhaseCatalog::emplace().
     */
    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    /**
     * \brief Borrow the governing model identifier.
     * \return Identifier owned by this descriptor.
     */
    [[nodiscard]] const ModelId& model_id() const noexcept { return model_id_; }
    /**
     * \brief Borrow the compiled discretization identifier.
     * \return Identifier owned by this descriptor.
     */
    [[nodiscard]] const DiscretizationId& discretization_id() const noexcept { return discretization_id_; }

private:
    /** \brief Construct the complete metadata value during catalog registration. */
    PhaseDescriptor(PhaseId id, std::string name, ModelId model_id, DiscretizationId discretization_id) :
        id_(id),
        name_(std::move(name)),
        model_id_(std::move(model_id)),
        discretization_id_(std::move(discretization_id))
    {
    }

    template<int dim, typename Number> friend class PhaseCatalog;

    /** \brief Context-local dense identity. */
    PhaseId id_;
    /** \brief Application-supplied name, preserved exactly. */
    std::string name_;
    /** \brief Stable bulk-model identifier. */
    ModelId model_id_;
    /** \brief Stable compiled-discretization identifier. */
    DiscretizationId discretization_id_;
};

/**
 * \brief Base class for one physical phase governed by one coherent bulk model.
 * \tparam dim Spatial dimension.
 * \tparam Number Scalar number type shared by the coupled system.
 *
 * Derive a concrete phase to hold model parameters and, in later interfaces,
 * phase-specific finite-element state and operators. PhaseCatalog owns each
 * instance and supplies its immutable PhaseDescriptor as the first constructor
 * argument. A concrete type must also provide noexcept static constexpr
 * model_identifier() and discretization_identifier() functions returning
 * std::string_view.
 *
 * The base provides type-erased ownership and common metadata. Concrete phase
 * types retain statically compiled numerical kernels; virtual dispatch is
 * intended only for coarse phase or work-range operations, not cell or
 * quadrature-point loops.
 *
 * \par Defining a phase type
 * \code{.cpp}
 * class FluidPhase final : public rift::Phase<2, double> {
 * public:
 *   static constexpr std::string_view model_identifier() noexcept {
 *     return "incompressible-navier-stokes";
 *   }
 *
 *   static constexpr std::string_view discretization_identifier() noexcept {
 *     return "velocity-pressure-q2-q1";
 *   }
 *
 *   FluidPhase(rift::PhaseDescriptor descriptor, const double viscosity)
 *       : Phase(std::move(descriptor)), viscosity_(viscosity) {}
 *
 * private:
 *   double viscosity_;
 * };
 * \endcode
 */
template<int dim, typename Number> class Phase {
public:
    /** \brief Phase objects have unique catalog ownership and cannot be copied. */
    Phase(const Phase&) = delete;
    /** \brief Phase ownership cannot be replaced by copying. */
    Phase& operator=(const Phase&) = delete;
    /** \brief Preserve phase identity and address after registration. */
    Phase(Phase&&) = delete;
    /** \brief Phase ownership cannot be replaced by moving. */
    Phase& operator=(Phase&&) = delete;
    /** \brief Enable destruction through the abstract phase base. */
    virtual ~Phase() = 0;

    /**
     * \brief Borrow this phase's immutable common metadata.
     * \return Reference valid for the lifetime of this phase.
     */
    [[nodiscard]] const PhaseDescriptor& descriptor() const noexcept { return descriptor_; }

protected:
    /**
     * \brief Construct a concrete phase from catalog-assigned metadata.
     * \param descriptor Complete immutable descriptor for this phase.
     */
    explicit Phase(PhaseDescriptor descriptor) : descriptor_(std::move(descriptor)) {}

private:
    /** \brief Authoritative immutable common metadata. */
    PhaseDescriptor descriptor_;
};

template<int dim, typename Number> Phase<dim, Number>::~Phase() = default;

} // namespace rift
