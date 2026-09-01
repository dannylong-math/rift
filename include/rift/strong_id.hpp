#pragma once

/**
 * \file
 * \brief Strong integer identifiers shared by Rift components.
 */

#include <compare>
#include <concepts>
#include <cstdint>

namespace rift {

/**
 * \brief Give an integer identifier a distinct compile-time meaning.
 *
 * Different tag types produce incompatible identifier types even when their
 * integer representations are identical. Values are created explicitly from
 * zero-based indices and do not convert implicitly to or from integers.
 *
 * \tparam Tag semantic domain that distinguishes this identifier.
 * \tparam Representation unsigned integer type stored by the identifier.
 */
template<class Tag, std::unsigned_integral Representation = std::uint32_t> class StrongId {
public:
    /** \brief Integer type stored by this identifier. */
    using representation_type = Representation;

    /**
     * \brief Create an identifier from an index supplied by its owning subsystem.
     *
     * This value type does not perform a range check; the subsystem assigning
     * identifiers owns that validation.
     *
     * \param value zero-based integer representation.
     * \return identifier containing `value`.
     */
    [[nodiscard]] static constexpr StrongId from_index(const Representation value) noexcept { return StrongId(value); }

    /**
     * \brief Read the stored zero-based index.
     *
     * \return integer representation of this identifier.
     */
    [[nodiscard]] constexpr Representation value() const noexcept { return value_; }

    /**
     * \brief Compare identifiers from the same semantic domain.
     *
     * \return ordering of the stored integer representations.
     */
    friend constexpr auto operator<=>(const StrongId&, const StrongId&) noexcept = default;

private:
    /**
     * \brief Store an index without permitting implicit integer conversion.
     *
     * \param value zero-based integer representation.
     */
    explicit constexpr StrongId(const Representation value) noexcept : value_(value) {}

    /** \brief Zero-based integer representation. */
    Representation value_;
};

} // namespace rift
