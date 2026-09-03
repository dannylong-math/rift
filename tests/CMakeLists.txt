include(FetchContent)

find_package(MPI REQUIRED COMPONENTS CXX)

FetchContent_Declare(ut
    GIT_REPOSITORY https://github.com/boost-ext/ut.git
    GIT_TAG v2.3.1
)
FetchContent_MakeAvailable(ut)

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

foreach(test_source IN LISTS TEST_SOURCES)
    get_filename_component(test_name "${test_source}" NAME_WE)

    add_executable(${test_name} "${test_source}")
    target_link_libraries(${test_name} rift::rift Boost::ut)
    deal_ii_setup_target(${test_name})

    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES LABELS "unit")
endforeach()

file(GLOB MPI_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/mpi/*.cpp")

function(add_rift_mpi_test test_target test_name mpi_process_count)
    add_test(
        NAME "${test_name}"
        COMMAND "${MPIEXEC_EXECUTABLE}"
            "${MPIEXEC_NUMPROC_FLAG}" "${mpi_process_count}"
            ${MPIEXEC_PREFLAGS}
            "$<TARGET_FILE:${test_target}>"
            ${MPIEXEC_POSTFLAGS}
            ${ARGN}
        COMMAND_EXPAND_LISTS
    )
    set_tests_properties("${test_name}" PROPERTIES
        LABELS "mpi;unit"
        PROCESSORS "${mpi_process_count}"
        TIMEOUT 60
    )
endfunction()

foreach(test_source IN LISTS MPI_TEST_SOURCES)
    get_filename_component(test_name "${test_source}" NAME_WE)

    add_executable(${test_name} "${test_source}")
    target_link_libraries(${test_name} rift::rift Boost::ut)
    deal_ii_setup_target(${test_name})

    foreach(mpi_process_count RANGE 1 3)
        if(mpi_process_count EQUAL 1)
            set(rank_suffix "rank")
        else()
            set(rank_suffix "ranks")
        endif()
        set(mpi_test_name "${test_name}_${mpi_process_count}_${rank_suffix}")

        add_rift_mpi_test("${test_name}" "${mpi_test_name}" "${mpi_process_count}")
    endforeach()
endforeach()

if(TARGET phase_graph_agreement_test)
    add_rift_mpi_test(phase_graph_agreement_test phase_graph_agreement_test_no_interfaces_1_rank 1 no_interfaces)
    add_rift_mpi_test(phase_graph_agreement_test phase_graph_agreement_test_accept_all_policy_1_rank 1
        accept_all_policy)
    add_rift_mpi_test(phase_graph_agreement_test phase_graph_agreement_test_no_phases_1_rank 1 no_phases)
    add_rift_mpi_test(phase_graph_agreement_test phase_graph_agreement_test_field_errors_3_ranks 3 field_errors)
    add_rift_mpi_test(phase_graph_agreement_test phase_graph_agreement_test_missing_compatibility_1_rank 1
        missing_compatibility)
    add_rift_mpi_test(phase_graph_agreement_test phase_graph_agreement_test_input_mismatch_2_ranks 2 input_mismatch)
    add_rift_mpi_test(phase_graph_agreement_test phase_graph_agreement_test_compatibility_failures_2_ranks 2
        compatibility_failures)
    add_rift_mpi_test(phase_graph_agreement_test phase_graph_agreement_test_nonstandard_exception_1_rank 1
        nonstandard_exception)
    add_rift_mpi_test(phase_graph_agreement_test phase_graph_agreement_test_compatibility_mismatch_3_ranks 3
        compatibility_mismatch)
endif()

if(TARGET phase_support_agreement_test)
    add_rift_mpi_test(phase_support_agreement_test phase_support_agreement_test_no_phase_graph_1_rank 1)
    set_tests_properties(phase_support_agreement_test_no_phase_graph_1_rank PROPERTIES
        ENVIRONMENT "RIFT_PHASE_SUPPORT_TEST_MODE=no_phase_graph"
    )
endif()
