#pragma once

/**
 * \file
 * \brief Private ownership shared by provisional and finalized field spaces.
 */

#include <rift/field_group_space.hpp>
#include <vector>

namespace rift::detail {

/** \brief Own both canonical field-space categories after atomic construction. */
template<int dim>
    requires(dim == 2 || dim == 3)
struct FieldSpaceStorage {
    /** \brief Support-restricted spaces in canonical descriptor order. */
    std::vector<PhaseSupportFieldGroupSpace<dim>> phase_support_fields;
    /** \brief Geometry spaces in canonical descriptor order. */
    std::vector<GeometryFieldGroupSpace<dim>> geometry_fields;
};

} // namespace rift::detail
