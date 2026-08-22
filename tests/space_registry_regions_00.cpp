#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>

template<int dim> void check_regional_entries_finalize()
{
    using namespace boost::ut;

    const auto snapshot = rift::test::make_space_with_one_phase_field<dim>({{"pressure"}, {"compatibility"}});

    expect(snapshot.layout().regional_entries().size() == 2_u);
    expect(snapshot.layout().regional_entries()[0].name == "compatibility");
    expect(snapshot.layout().regional_entries()[1].name == "pressure");
    expect(snapshot.layout().regional_entries()[0].id == rift::RegionalEntryId::from_index(0));
}

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    using namespace boost::ut;

    "regional entries join the final layout in stable order in 2D and 3D"_test = [] {
        check_regional_entries_finalize<2>();
        check_regional_entries_finalize<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
