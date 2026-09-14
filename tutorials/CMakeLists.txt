add_executable(rift_tutorial_01_foundations 01_foundations.cpp)
target_link_libraries(rift_tutorial_01_foundations rift::rift)
deal_ii_setup_target(rift_tutorial_01_foundations)

if(BUILD_TESTING)
    find_package(MPI REQUIRED COMPONENTS CXX)
    foreach(mpi_process_count RANGE 1 2)
        if(mpi_process_count EQUAL 1)
            set(rank_suffix "rank")
        else()
            set(rank_suffix "ranks")
        endif()
        set(test_name "tutorial_01_foundations_${mpi_process_count}_${rank_suffix}")
        add_test(
            NAME "${test_name}"
            COMMAND "${MPIEXEC_EXECUTABLE}"
                "${MPIEXEC_NUMPROC_FLAG}" "${mpi_process_count}"
                ${MPIEXEC_PREFLAGS}
                "$<TARGET_FILE:rift_tutorial_01_foundations>"
                ${MPIEXEC_POSTFLAGS}
            COMMAND_EXPAND_LISTS
        )
        set_tests_properties("${test_name}" PROPERTIES
            LABELS "mpi;tutorial"
            PROCESSORS "${mpi_process_count}"
            TIMEOUT 60
        )
    endforeach()
endif()
