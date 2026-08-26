find_package(MPI REQUIRED COMPONENTS CXX)

set(RIFT_TUTORIAL_TARGETS
    tutorial-001
    tutorial-002
    tutorial-003
)

foreach(tutorial_target IN LISTS RIFT_TUTORIAL_TARGETS)
    add_executable(${tutorial_target} "${tutorial_target}.cpp")
    target_link_libraries(${tutorial_target}
        rift::rift
        rift_warnings
        rift_sanitizers
    )
    deal_ii_setup_target(${tutorial_target})

    if(BUILD_TESTING)
        string(REGEX REPLACE "^tutorial-0*" "" tutorial_number "${tutorial_target}")
        add_test(NAME ${tutorial_target}
            COMMAND
                ${MPIEXEC_EXECUTABLE}
                ${MPIEXEC_NUMPROC_FLAG} 2
                ${MPIEXEC_PREFLAGS}
                $<TARGET_FILE:${tutorial_target}>
                ${MPIEXEC_POSTFLAGS}
        )
        set_tests_properties(${tutorial_target} PROPERTIES
            LABELS "tutorial;mpi"
            PROCESSORS 2
            TIMEOUT 60
            PASS_REGULAR_EXPRESSION "Tutorial ${tutorial_number} complete:"
        )
    endif()
endforeach()
