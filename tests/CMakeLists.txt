include(FetchContent)

FetchContent_Declare(ut
    GIT_REPOSITORY https://github.com/boost-ext/ut.git
    GIT_TAG v2.3.1
)
FetchContent_MakeAvailable(ut)
find_package(MPI REQUIRED COMPONENTS CXX)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(rift_add_test_executable test_name test_source)
    add_executable(${test_name} "${test_source}")
    target_link_libraries(${test_name}
        rift::rift
        rift_warnings
        rift_sanitizers
        rift_llvm_coverage
        Boost::ut
    )
    deal_ii_setup_target(${test_name})
endfunction()

function(rift_add_serial_alias test_name test_target test_mode)
    if(NOT TARGET ${test_target})
        message(FATAL_ERROR "Serial test ${test_name} aliases unknown target ${test_target}")
    endif()
    add_test(NAME ${test_name} COMMAND $<TARGET_FILE:${test_target}> ${test_mode})
    set_tests_properties(${test_name} PROPERTIES LABELS "serial")
endfunction()

function(rift_add_mpi_alias test_name test_target process_count test_mode)
    if(process_count LESS 2)
        message(FATAL_ERROR "MPI test ${test_name} must declare at least two processes")
    endif()
    if(NOT TARGET ${test_target})
        message(FATAL_ERROR "MPI test ${test_name} aliases unknown target ${test_target}")
    endif()
    add_test(NAME ${test_name}
        COMMAND
            ${MPIEXEC_EXECUTABLE}
            ${MPIEXEC_NUMPROC_FLAG} ${process_count}
            ${MPIEXEC_PREFLAGS}
            $<TARGET_FILE:${test_target}>
            ${MPIEXEC_POSTFLAGS}
            ${test_mode}
    )
    set_tests_properties(${test_name} PROPERTIES
        LABELS "mpi"
        PROCESSORS ${process_count}
        TIMEOUT 60
    )
endfunction()

function(rift_add_fatal_mpi_alias test_name test_target process_count test_mode operation expected_status)
    if(process_count LESS 2)
        message(FATAL_ERROR "MPI fatal-path test ${test_name} must declare at least two processes")
    endif()
    if(NOT TARGET ${test_target})
        message(FATAL_ERROR "MPI fatal-path test ${test_name} aliases unknown target ${test_target}")
    endif()
    set(expected_marker
        "RIFT_FATAL_ORACLE operation=${operation} status=${expected_status}"
    )
    add_test(NAME ${test_name}
        COMMAND
            ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/scripts/fatal_mpi_test_wrapper.py"
            --expected-marker "${expected_marker}"
            --timeout-seconds 10
            --
            ${MPIEXEC_EXECUTABLE}
            ${MPIEXEC_NUMPROC_FLAG} ${process_count}
            ${MPIEXEC_PREFLAGS}
            $<TARGET_FILE:${test_target}>
            ${MPIEXEC_POSTFLAGS}
            ${test_mode}
    )
    set_tests_properties(${test_name} PROPERTIES
        LABELS "mpi"
        PROCESSORS ${process_count}
        TIMEOUT 15
    )
endfunction()

# Only reviewed subject runners may be executable C++ sources.
file(GLOB ROOT_CPP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB SUBJECT_RUNNERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_run_tests.cpp")
if(NOT ROOT_CPP_SOURCES STREQUAL SUBJECT_RUNNERS)
    message(FATAL_ERROR "Every root test .cpp must be a reviewed *_run_tests.cpp subject runner")
endif()

file(GLOB MPI_CPP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/mpi/*.cpp")
set(EXPECTED_MPI_RUNNER "${CMAKE_CURRENT_SOURCE_DIR}/mpi/mpi_test_infrastructure_run_tests.cpp")
if(NOT MPI_CPP_SOURCES STREQUAL EXPECTED_MPI_RUNNER)
    message(FATAL_ERROR "Only mpi_test_infrastructure_run_tests.cpp may remain under tests/mpi")
endif()

file(GLOB COVERAGE_CPP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/coverage/*.cpp")
set(EXPECTED_COVERAGE_RUNNER "${CMAKE_CURRENT_SOURCE_DIR}/coverage/coverage_completeness_run_tests.cpp")
if(NOT COVERAGE_CPP_SOURCES STREQUAL EXPECTED_COVERAGE_RUNNER)
    message(FATAL_ERROR "Only coverage_completeness_run_tests.cpp may remain under tests/coverage")
endif()

foreach(test_source IN LISTS SUBJECT_RUNNERS)
    get_filename_component(test_name "${test_source}" NAME_WE)
    rift_add_test_executable(${test_name} "${test_source}")
    rift_add_serial_alias(${test_name} ${test_name} serial)
endforeach()

rift_add_serial_alias(
    run_configuration_uninitialized
    run_configuration_run_tests
    uninitialized
)
set_tests_properties(run_configuration_uninitialized PROPERTIES TIMEOUT 60)
rift_add_serial_alias(
    run_configuration_finalized
    run_configuration_run_tests
    finalized
)
set_tests_properties(run_configuration_finalized PROPERTIES TIMEOUT 60)

rift_add_test_executable(
    mpi_test_infrastructure_run_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/mpi/mpi_test_infrastructure_run_tests.cpp"
)

rift_add_mpi_alias(mpi_test_infrastructure_2_ranks mpi_test_infrastructure_run_tests 2 mpi-2)
rift_add_mpi_alias(run_configuration_mpi_2_ranks run_configuration_run_tests 2 mpi-2)
rift_add_mpi_alias(phase_graph_mpi_2_ranks phase_graph_run_tests 2 mpi-2)
rift_add_mpi_alias(phase_graph_mpi_3_ranks phase_graph_run_tests 3 mpi-3)
rift_add_mpi_alias(mesh_snapshot_mpi_2_ranks mesh_snapshot_run_tests 2 mpi-2)
rift_add_mpi_alias(space_registry_mpi_2_ranks space_registry_run_tests 2 mpi-2)
rift_add_mpi_alias(space_registry_mpi_3_ranks space_registry_run_tests 3 mpi-3)
rift_add_mpi_alias(state_store_mpi_2_ranks state_store_run_tests 2 mpi-2)
rift_add_mpi_alias(state_store_mpi_3_ranks state_store_run_tests 3 mpi-3)
rift_add_mpi_alias(state_transaction_mpi_2_ranks state_transaction_run_tests 2 mpi-2)
rift_add_mpi_alias(state_transaction_mpi_3_ranks state_transaction_run_tests 3 mpi-3)

rift_add_fatal_mpi_alias(
    space_registry_begin_fatal_00_mpi space_registry_run_tests 2 fatal-begin begin 34
)
rift_add_fatal_mpi_alias(
    space_registry_finalize_fatal_00_mpi space_registry_run_tests 2 fatal-finalize finalize 34
)
rift_add_fatal_mpi_alias(
    state_factory_fatal_00_mpi state_store_run_tests 2 fatal-state-factory state_factory 34
)
rift_add_fatal_mpi_alias(
    state_factory_dependency_fatal_00_mpi state_store_run_tests 2
    fatal-state-factory-dependency state_factory_dependency 15
)
rift_add_fatal_mpi_alias(
    state_begin_fatal_00_mpi state_store_run_tests 2 fatal-state-begin state_begin 34
)
rift_add_fatal_mpi_alias(
    state_publish_fatal_00_mpi state_store_run_tests 2 fatal-state-publish state_publish 34
)
rift_add_fatal_mpi_alias(
    state_seal_fatal_00_mpi state_transaction_run_tests 2 fatal-state-seal state_seal 34
)
rift_add_fatal_mpi_alias(
    state_regional_sync_fatal_00_mpi state_transaction_run_tests 2
    fatal-state-regional-sync state_regional_sync 15
)
rift_add_fatal_mpi_alias(
    state_regional_staging_fatal_00_mpi state_transaction_run_tests 2
    fatal-state-regional-staging state_regional_staging 34
)

if(ENABLE_LLVM_COVERAGE)
    add_executable(coverage_completeness_run_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/coverage/coverage_completeness_run_tests.cpp"
    )
    target_link_libraries(coverage_completeness_run_tests
        "-Wl,--whole-archive"
        rift::rift
        "-Wl,--no-whole-archive"
        rift_warnings
        rift_llvm_coverage
        Boost::ut
    )
    deal_ii_setup_target(coverage_completeness_run_tests)
    add_test(NAME coverage_completeness_run_tests
        COMMAND $<TARGET_FILE:coverage_completeness_run_tests> coverage
    )
    set_tests_properties(coverage_completeness_run_tests PROPERTIES
        LABELS "coverage"
        TIMEOUT 60
    )
endif()

file(GLOB COVERAGE_TOOL_TESTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tooling/merge_llvm_cov_*.py"
)
list(APPEND COVERAGE_TOOL_TESTS
    "${CMAKE_CURRENT_SOURCE_DIR}/tooling/coverage_test_manifest_00.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tooling/coverage_test_runner_00.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tooling/fatal_mpi_test_wrapper_00.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tooling/gcovr_miss_inventory_00.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tooling/subject_runner_legacy_names_00.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tooling/subject_runner_layout_00.py"
)
foreach(tool_test IN LISTS COVERAGE_TOOL_TESTS)
    get_filename_component(tool_test_name "${tool_test}" NAME_WE)
    add_test(NAME ${tool_test_name}
        COMMAND ${Python3_EXECUTABLE} "${tool_test}"
    )
    set_tests_properties(${tool_test_name} PROPERTIES LABELS "tooling")
endforeach()

add_test(NAME subject_runner_ctest_matrix_00
    COMMAND
        ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tooling/subject_runner_ctest_matrix_00.py"
        --build-dir "${CMAKE_BINARY_DIR}"
)
set_tests_properties(subject_runner_ctest_matrix_00 PROPERTIES LABELS "tooling")

add_test(NAME subject_runner_focused_vv_00
    COMMAND
        ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tooling/subject_runner_focused_vv_00.py"
        --build-dir "${CMAKE_BINARY_DIR}"
)
set_tests_properties(subject_runner_focused_vv_00 PROPERTIES LABELS "tooling")

add_test(NAME subject_runner_reverse_order_00
    COMMAND
        ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tooling/subject_runner_reverse_order_00.py"
        --runner "Run configuration=$<TARGET_FILE:run_configuration_run_tests>"
        --runner "Phase graph=$<TARGET_FILE:phase_graph_run_tests>"
        --runner "Mesh snapshot=$<TARGET_FILE:mesh_snapshot_run_tests>"
        --runner "Space registry=$<TARGET_FILE:space_registry_run_tests>"
        --runner "Space snapshot=$<TARGET_FILE:space_snapshot_run_tests>"
        --runner "State snapshot=$<TARGET_FILE:state_snapshot_run_tests>"
        --runner "State store=$<TARGET_FILE:state_store_run_tests>"
        --runner "Mutable state transaction=$<TARGET_FILE:state_transaction_run_tests>"
)
set_tests_properties(subject_runner_reverse_order_00 PROPERTIES
    LABELS "tooling"
    TIMEOUT 120
)

add_test(NAME subject_runner_verbose_suites_00
    COMMAND
        ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tooling/subject_runner_verbose_suites_00.py"
        --build-dir "${CMAKE_BINARY_DIR}"
)
set_tests_properties(subject_runner_verbose_suites_00 PROPERTIES
    LABELS "tooling"
    TIMEOUT 120
)

add_test(NAME tutorial_layout_00
    COMMAND
        ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tooling/tutorial_layout_00.py"
        --build-dir "${CMAKE_BINARY_DIR}"
)
set_tests_properties(tutorial_layout_00 PROPERTIES LABELS "tooling")
