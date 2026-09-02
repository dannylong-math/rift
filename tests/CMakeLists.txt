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

        add_test(
            NAME "${mpi_test_name}"
            COMMAND "${MPIEXEC_EXECUTABLE}"
                "${MPIEXEC_NUMPROC_FLAG}" "${mpi_process_count}"
                ${MPIEXEC_PREFLAGS}
                "$<TARGET_FILE:${test_name}>"
                ${MPIEXEC_POSTFLAGS}
            COMMAND_EXPAND_LISTS
        )
        set_tests_properties("${mpi_test_name}" PROPERTIES
            LABELS "mpi;unit"
            PROCESSORS "${mpi_process_count}"
            TIMEOUT 60
        )
    endforeach()
endforeach()
