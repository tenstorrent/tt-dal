cmake_minimum_required(VERSION 3.21)

project(
    ttdal
    VERSION 0.1.0
    DESCRIPTION "Tenstorrent Device Access Library"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED YES)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS include/*.h)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.c)
set_property(GLOBAL APPEND PROPERTY FORMATTED ${SOURCES} ${HEADERS})

# Library
add_library(ttdal ${SOURCES})

target_include_directories(ttdal
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/tt-kmd
)

set_target_properties(ttdal PROPERTIES
    C_STANDARD 23
    C_STANDARD_REQUIRED YES
    POSITION_INDEPENDENT_CODE ON
)

# Install
install(TARGETS ttdal
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES ${HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Tests
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(tests EXCLUDE_FROM_ALL)
endif()

# Examples
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(examples EXCLUDE_FROM_ALL)
endif()

# Format
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    get_property(FORMATTED GLOBAL PROPERTY FORMATTED)
    add_custom_target(fmt
        COMMAND ${CLANG_FORMAT} -i ${FORMATTED}
        COMMENT "Formatting source files"
        VERBATIM
    )
endif()

# Docs
find_package(Doxygen)
if(DOXYGEN_FOUND)
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_MAN NO)
    set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/docs)
    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_JAVADOC_AUTOBRIEF YES)
    set(DOXYGEN_MARKDOWN_SUPPORT YES)
    set(DOXYGEN_BUILTIN_STL_SUPPORT YES)
    set(DOXYGEN_PROJECT_NAME "Tenstorrent Device Access Library")
    set(DOXYGEN_PROJECT_NUMBER ${PROJECT_VERSION})
    set(DOXYGEN_PROJECT_BRIEF "Low-level C API for Tenstorrent accelerators")

    doxygen_add_docs(docs ${HEADERS})
endif()
