set(RIFT_SPHINX_VENV "${RIFT_DEPENDENCIES_DIR}/docs")

find_program(SPHINX_BUILD_EXECUTABLE
    NAMES sphinx-build
    HINTS "${RIFT_SPHINX_VENV}/bin"
    NO_DEFAULT_PATH
)

if(NOT SPHINX_BUILD_EXECUTABLE)
    find_program(SPHINX_BUILD_EXECUTABLE NAMES sphinx-build)
endif()

if(NOT SPHINX_BUILD_EXECUTABLE)
    message(FATAL_ERROR
        "sphinx-build was not found. Install the documentation dependencies with:\n"
        "  ./scripts/install_dependencies.sh --docs-only"
    )
endif()

file(GLOB_RECURSE RIFT_DOCUMENTATION_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rst"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)
file(GLOB_RECURSE RIFT_PUBLIC_HEADERS CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/include/*.h"
    "${PROJECT_SOURCE_DIR}/include/*.hpp"
)

set(RIFT_DOCUMENTATION_OUTPUT "${CMAKE_BINARY_DIR}/html")
set(RIFT_DOCUMENTATION_DOCTREES "${CMAKE_BINARY_DIR}/doctrees")

add_custom_target(docs
    COMMAND
        "${SPHINX_BUILD_EXECUTABLE}"
        --builder html
        --doctree-dir "${RIFT_DOCUMENTATION_DOCTREES}"
        --fresh-env
        --fail-on-warning
        --keep-going
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${RIFT_DOCUMENTATION_OUTPUT}"
    DEPENDS ${RIFT_DOCUMENTATION_SOURCES} ${RIFT_PUBLIC_HEADERS}
    COMMENT "Building Rift documentation with Sphinx"
    VERBATIM
)

message(STATUS "Sphinx executable: ${SPHINX_BUILD_EXECUTABLE}")
message(STATUS "Documentation output: ${RIFT_DOCUMENTATION_OUTPUT}/index.html")
