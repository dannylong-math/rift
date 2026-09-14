include(FetchContent)

find_package(MPI REQUIRED COMPONENTS CXX)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG 317ac1ed4c0bb6e6b91eafc817e05c488feffcb3 # v3.16.0
)
FetchContent_MakeAvailable(Catch2)

# Apply Rift's clang-tidy policy to first-party code, not the fetched framework.
set_target_properties(Catch2 Catch2WithMain PROPERTIES CXX_CLANG_TIDY "")

# Treat framework headers and their macro expansions as third-party code in
# consumers too; keep clang-tidy enabled for Rift's test bodies.
get_target_property(catch2_include_directories Catch2 INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(Catch2 SYSTEM INTERFACE ${catch2_include_directories})
unset(catch2_include_directories)

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")

foreach(test_source IN LISTS TEST_SOURCES)
    get_filename_component(test_name "${test_source}" NAME_WE)

    add_executable(${test_name} "${test_source}")
    target_link_libraries(${test_name} rift::rift Catch2::Catch2WithMain)
    deal_ii_setup_target(${test_name})

    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES LABELS "unit")
endforeach()

file(GLOB MPI_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/mpi/*_test.cpp")

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
    # MPI tests provide a main that initializes MPI, runs Catch::Session,
    # and finalizes MPI. Catch2WithMain cannot manage that lifetime.
    target_link_libraries(${test_name} rift::rift Catch2::Catch2)
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
