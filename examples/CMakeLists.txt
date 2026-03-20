# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

# Auto-discover examples (each is a directory with src/main.c)
file(GLOB DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)

set(TARGETS)
foreach(dir ${DIRS})
    set(src ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/src/main.c)
    if(EXISTS ${src})
        add_executable(examples.${dir} ${src})
        target_link_libraries(examples.${dir} PRIVATE ttdal)
        set_target_properties(examples.${dir} PROPERTIES OUTPUT_NAME ${dir})
        list(APPEND TARGETS examples.${dir})
        set_property(GLOBAL APPEND PROPERTY FORMATTED ${src})
    endif()
endforeach()

add_custom_target(examples DEPENDS ${TARGETS})
