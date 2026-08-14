"""Sphinx configuration for the Rift documentation."""

from pathlib import Path


DOCS_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = DOCS_DIR.parent
INCLUDE_DIR = PROJECT_ROOT / "include"
PUBLIC_HEADERS = sorted(INCLUDE_DIR.rglob("*.h")) + sorted(INCLUDE_DIR.rglob("*.hpp"))
API_INPUT = "".join(
    f'#include "{header.relative_to(INCLUDE_DIR).as_posix()}"\n'
    for header in PUBLIC_HEADERS
)

project = "Rift"
author = "The Rift contributors"
copyright = "2026, The Rift contributors"
version = "0.1"
release = "0.1.0"

extensions = [
    "sphinx_immaterial",
    "sphinx_immaterial.apidoc.cpp.apigen",
]

html_theme = "sphinx_immaterial"
html_title = "Rift documentation"
html_theme_options = {
    # Use system fonts so documentation builds remain offline and reproducible.
    "font": False,
    "features": [
        "navigation.expand",
        "navigation.sections",
        "navigation.top",
        "navigation.footer",
        "toc.follow",
        "toc.sticky",
        "content.code.copy",
        "content.tooltips",
    ],
    "palette": [
        {
            "media": "(prefers-color-scheme: light)",
            "scheme": "default",
            "primary": "blue-grey",
            "accent": "deep-orange",
            "toggle": {
                "icon": "material/brightness-7",
                "name": "Switch to dark mode",
            },
        },
        {
            "media": "(prefers-color-scheme: dark)",
            "scheme": "slate",
            "primary": "blue-grey",
            "accent": "amber",
            "toggle": {
                "icon": "material/brightness-4",
                "name": "Switch to light mode",
            },
        },
    ],
    "toc_title_is_page_title": True,
}

exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]
highlight_language = "cpp"
primary_domain = "cpp"

# Sphinx-Immaterial parses Rift's public headers directly with libclang. API
# comments use reStructuredText; no Doxygen preprocessing step is involved.
cpp_apigen_configs = [
    {
        "document_prefix": "api/generated/",
        "api_parser_config": {
            "input_content": API_INPUT,
            "compiler_flags": [
                "-std=c++23",
                "-x",
                "c++",
                "-I",
                str(INCLUDE_DIR),
            ],
            "include_directory_map": {f"{INCLUDE_DIR}/": ""},
            "allow_paths": [r"^rift/"],
            "allow_symbols": [r"^rift(::.*)?$"],
            "disallow_namespaces": [r"^std$"],
        },
    }
]

cpp_apigen_rst_prolog = """
.. default-role:: cpp:expr

.. default-literal-role:: cpp

.. highlight:: cpp
"""

nitpicky = True
nitpick_ignore = [
    # Sphinx's C++ domain does not define namespace objects, but generated
    # signatures still try to resolve their namespace prefixes.
    ("cpp:identifier", "rift"),
]
