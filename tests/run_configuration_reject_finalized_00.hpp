#pragma once

#include <boost/ut.hpp>
#include <rift/run_configuration.hpp>

namespace rift_test::run_configuration_reject_finalized_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "run construction reports that MPI is already finalized"_test = [] {
        const auto result = rift::RunConfiguration::create(MPI_COMM_SELF);

        expect(!result.has_value());
        expect(result.error().code == rift::RunConfigurationErrorCode::mpi_finalized);
    };
}

} // namespace rift_test::run_configuration_reject_finalized_00
