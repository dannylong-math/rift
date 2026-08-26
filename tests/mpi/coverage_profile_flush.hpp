#pragma once

extern "C" int write_coverage_profile() asm("__llvm_profile_write_file") __attribute__((weak));

namespace rift_test::mpi {

inline void flush_coverage_profile()
{
    if (write_coverage_profile != nullptr) {
        static_cast<void>(write_coverage_profile());
    }
}

} // namespace rift_test::mpi
