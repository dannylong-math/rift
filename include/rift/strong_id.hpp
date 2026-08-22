#pragma once

/**
 * \file
 * \brief Strong integer identities shared by Rift subsystems.
 */

#include <compare>
#include <concepts>
#include <cstdint>

namespace rift {

/**
 * \brief Give a compact integer identifier its own compile-time semantic type.
 *
 * \par When to use
 * Define a `StrongId` alias when two integer identifiers have different
 * meanings and must not be mixed accidentally. Rift uses this template for
 * phase, field-group, epoch, and snapshot identities.
 *
 * \par Typical use
 * \code{.cpp}
 * struct CellIdTag {};
 * using CellId = rift::StrongId<CellIdTag>;
 *
 * const CellId first = CellId::from_index(0);
 * const CellId second = CellId::from_index(1);
 * const bool ids_are_ordered = first < second;
 * const std::uint32_t first_index = first.value();
 * \endcode
 *
 * \par Important behavior
 * The tag occupies no storage, so a `StrongId` has the same size as
 * `Representation`. Comparisons are available only between identifiers with
 * the same tag. Integer conversion is explicit through `from_index()` and
 * `value()`.
 *
 * \tparam Tag semantic domain that distinguishes this identifier from other identifiers.
 * \tparam Representation unsigned integer type used for storage.
 */
template<class Tag, std::unsigned_integral Representation = std::uint32_t> class StrongId {
public:
    /**
     * \brief Unsigned integer type used to store this identifier.
     */
    using representation_type = Representation;

    /**
     * \brief Recreate an identifier from an index owned by the same subsystem.
     *
     * \param value zero-based integer representation.
     * \return identifier containing `value`.
     */
    static constexpr StrongId from_index(const Representation value) noexcept { return StrongId(value); }

    /**
     * \brief Read the identifier's zero-based integer representation.
     *
     * \return stored integer value.
     */
    [[nodiscard]] constexpr Representation value() const noexcept { return value_; }

    /**
     * \brief Compare two identifiers from the same semantic domain.
     *
     * \return strong ordering of the stored integer representations.
     */
    friend constexpr auto operator<=>(const StrongId&, const StrongId&) noexcept = default;

private:
    /**
     * \brief Store a subsystem index without permitting implicit conversion.
     *
     * \param value zero-based integer representation to store.
     */
    explicit constexpr StrongId(const Representation value) noexcept : value_(value) {}

    /**
     * \brief Zero-based integer representation.
     */
    Representation value_;
};

} // namespace rift
