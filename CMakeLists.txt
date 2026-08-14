cmake_minimum_required(VERSION 3.21)

# This mode keeps the documentation build independent of the (large) deal.II
# installation. It is set by the "docs" preset.
option(RIFT_DOCUMENTATION_ONLY "Configure only the documentation target" OFF)

set(RIFT_DEPENDENCIES_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/.dependencies"
    CACHE PATH "Root of Rift's locally installed dependencies"
)

if(NOT RIFT_DOCUMENTATION_ONLY)
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
endif()

project(rift
    VERSION 0.1.0
    DESCRIPTION "Sharp-interface multiphase flow research code"
    LANGUAGES CXX
)

# Prevent in-source builds.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not allowed.\n"
        "Use: cmake --preset debug  (or release / docs)"
    )
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RIFT_BUILD_DOCUMENTATION "Add the Sphinx documentation target" OFF)

if(NOT RIFT_DOCUMENTATION_ONLY)
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

    option(ENABLE_CLANG_TIDY "Run clang-tidy during compilation" OFF)
    if(ENABLE_CLANG_TIDY)
        find_program(CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    endif()

    add_library(rift STATIC src/rift.cpp)
    add_library(rift::rift ALIAS rift)

    target_include_directories(rift PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_features(rift PUBLIC cxx_std_23)
    target_link_libraries(rift  rift_warnings rift_sanitizers)
    deal_ii_setup_target(rift)

    option(BUILD_TESTING "Build tests" ON)
    if(BUILD_TESTING)
        enable_testing()
        add_subdirectory(tests)
    endif()
endif()

if(RIFT_BUILD_DOCUMENTATION OR RIFT_DOCUMENTATION_ONLY)
    add_subdirectory(docs)
endif()
