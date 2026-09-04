cmake_minimum_required(VERSION 3.21)

set(RIFT_DEPENDENCIES_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/.dependencies"
    CACHE PATH "Root of Rift's locally installed dependencies"
)

if(NOT CMAKE_BUILD_TYPE)
    message(FATAL_ERROR
        "CMAKE_BUILD_TYPE must be Debug or Release. Use a Rift preset."
    )
endif()

if(NOT DEAL_II_DIR)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_rift_dealii_variant "debug")
    elseif(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
        set(_rift_dealii_variant "release")
    else()
        message(FATAL_ERROR
            "Unsupported CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'. "
            "Use Debug, Release, RelWithDebInfo, or MinSizeRel."
        )
    endif()

    set(DEAL_II_DIR
        "${RIFT_DEPENDENCIES_DIR}/install/dealii/${_rift_dealii_variant}/lib/cmake/deal.II"
        CACHE PATH "Path to the deal.II CMake package" FORCE
    )
    unset(_rift_dealii_variant)
endif()

find_package(deal.II 9.8.0 EXACT REQUIRED HINTS "${DEAL_II_DIR}")
deal_ii_initialize_cached_variables()

project(rift
    VERSION 0.1.0
    DESCRIPTION "Sharp-interface multiphase flow research code"
    LANGUAGES CXX
)

find_package(simdutf 9.0.0 EXACT REQUIRED CONFIG
    HINTS "${RIFT_DEPENDENCIES_DIR}/install/simdutf/lib/cmake/simdutf"
)

# Prevent in-source builds.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not allowed.\n"
        "Use: cmake --preset debug  (or release / release-max)"
    )
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep clangd pointed at the most recently configured code build.
file(REMOVE "${CMAKE_SOURCE_DIR}/compile_commands.json")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E create_symlink
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
)

add_library(rift_warnings INTERFACE)
target_compile_options(rift_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>
)

add_library(rift_sanitizers INTERFACE)
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)
if(ENABLE_SANITIZERS)
    target_compile_options(rift_sanitizers INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fsanitize=address,undefined;-fno-omit-frame-pointer>
    )
    target_link_options(rift_sanitizers INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fsanitize=address,undefined>
    )
endif()

add_library(rift_coverage INTERFACE)
option(ENABLE_COVERAGE "Instrument tests for code coverage" OFF)
if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(rift_coverage INTERFACE
            --coverage
            -O0
            -g
        )
        target_link_options(rift_coverage INTERFACE --coverage)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(rift_coverage INTERFACE
            -fprofile-instr-generate
            -fcoverage-mapping
            -O0
            -g
        )
        target_link_options(rift_coverage INTERFACE
            -fprofile-instr-generate
        )
    else()
        message(FATAL_ERROR "ENABLE_COVERAGE requires GCC or Clang")
    endif()
endif()

option(ENABLE_CLANG_TIDY "Run clang-tidy during compilation" OFF)
if(ENABLE_CLANG_TIDY)
    string(REGEX MATCH "^[0-9]+" CLANG_TIDY_COMPILER_MAJOR "${CMAKE_CXX_COMPILER_VERSION}")
    find_program(CLANG_TIDY_EXE NAMES "clang-tidy-${CLANG_TIDY_COMPILER_MAJOR}" clang-tidy REQUIRED)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

add_library(rift STATIC
    src/mesh_snapshot.cpp
    src/phase_graph.cpp
    src/phase_support.cpp
    src/rift.cpp
    src/space_draft.cpp
)
add_library(rift::rift ALIAS rift)

target_include_directories(rift PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(rift PUBLIC cxx_std_23)
target_link_libraries(rift
    rift_warnings
    rift_sanitizers
    rift_coverage
    simdutf::simdutf
)
deal_ii_setup_target(rift)

option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
