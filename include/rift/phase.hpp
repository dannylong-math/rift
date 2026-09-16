#pragma once

/**
 * \file
 * \brief Phase identity, immutable metadata, and the phase type-erasure base.
 */

#include <compare>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace rift {

template<int dim, typename Number> class PhaseCatalog;

/**
 * \brief Dense identity of a phase within one Context.
 *
 * IDs compare by their zero-based index. They are meaningful only for objects
 * associated with the same Context and cannot be constructed by application
 * code.
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

/** \brief Stable, human-readable identifier of a bulk phase model. */
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

/** \brief Stable, human-readable identifier of a compiled discretization. */
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
 * \brief Immutable common metadata for one registered phase.
 *
 * The sole PhaseCatalog for a Context constructs this value transactionally.
 * Consumers may copy it but cannot replace its fields after construction.
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

    /** \brief Return the Context-local phase identity. */
    [[nodiscard]] constexpr PhaseId id() const noexcept { return id_; }
    /** \brief Borrow the application-supplied phase name. */
    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    /** \brief Borrow the stable model identifier. */
    [[nodiscard]] const ModelId& model_id() const noexcept { return model_id_; }
    /** \brief Borrow the stable discretization identifier. */
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
 * \brief Abstract ownership and metadata boundary for one physical phase.
 * \tparam dim Spatial dimension.
 * \tparam Number Scalar number type shared by the coupled system.
 *
 * Concrete phase specializations retain statically compiled numerical kernels.
 * Virtual dispatch through this base is reserved for coarse phase or work-range
 * operations, not cell or quadrature-point loops.
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
