#pragma once

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/run_configuration.hpp>

namespace rift_test::run_configuration_reject_null_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "a null communicator is rejected without entering a collective"_test = [] {
        const auto result = rift::RunConfiguration::create(MPI_COMM_NULL);

        expect(!result.has_value());
        expect(result.error().code == rift::RunConfigurationErrorCode::null_communicator);
    };
}

} // namespace rift_test::run_configuration_reject_null_00
