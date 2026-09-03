/**
 * \file
 * \brief Immutable phase-support value and view implementation.
 */

#include <cstddef>
#include <deal.II/grid/cell_id.h>
#include <memory>
#include <rift/phase_support.hpp>
#include <span>
#include <utility>
#include <vector>

namespace rift {

PhaseSupport::PhaseSupport(const PhaseId phase, std::vector<dealii::CellId> cells,
                           const std::size_t requested_count) noexcept :
    phase_(phase), cells_(std::move(cells)), requested_count_(requested_count)
{
}

std::span<const dealii::CellId> PhaseSupport::requested_cells() const noexcept
{
    return std::span<const dealii::CellId>{cells_}.first(requested_count_);
}

std::span<const dealii::CellId> PhaseSupport::closure_added_cells() const noexcept
{
    return std::span<const dealii::CellId>{cells_}.subspan(requested_count_);
}

std::span<const dealii::CellId> PhaseSupport::closed_cells() const noexcept { return cells_; }

template<int dim>
    requires(dim == 2 || dim == 3)
PhaseSupportSet<dim>::PhaseSupportSet(std::shared_ptr<const MeshSnapshot<dim>> mesh,
                                      std::vector<PhaseSupport> supports) noexcept :
    mesh_(std::move(mesh)), supports_(std::move(supports))
{
}

template<int dim>
    requires(dim == 2 || dim == 3)
const MeshSnapshot<dim>& PhaseSupportSet<dim>::mesh_snapshot() const noexcept
{
    return *mesh_;
}

template<int dim>
    requires(dim == 2 || dim == 3)
std::span<const PhaseSupport> PhaseSupportSet<dim>::supports() const noexcept
{
    return supports_;
}

template<int dim>
    requires(dim == 2 || dim == 3)
const PhaseSupport& PhaseSupportSet<dim>::support(const PhaseId phase) const
{
    return supports_.at(phase.value());
}

template class PhaseSupportSet<2>;
template class PhaseSupportSet<3>;

} // namespace rift
