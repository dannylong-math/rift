#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>

namespace rift_test::space_registry_regions_00 {

template<int dim> void check_regional_entries_finalize()
{
    using namespace boost::ut;

    const auto snapshot = rift::test::make_space_with_one_phase_field<dim>({{"pressure"}, {"compatibility"}});

    expect(snapshot.layout().regional_entries().size() == 2_u);
    expect(snapshot.layout().regional_entries().front().name == "compatibility");
    expect(snapshot.layout().regional_entries().back().name == "pressure");
    expect(snapshot.layout().regional_entries().front().id == rift::RegionalEntryId::from_index(0));
}

} // namespace rift_test::space_registry_regions_00

namespace rift_test::space_registry_regions_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "regional entries join the final layout in stable order in 2D and 3D"_test = [] {
        check_regional_entries_finalize<2>();
        check_regional_entries_finalize<3>();
    };
}

} // namespace rift_test::space_registry_regions_00
