# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.c")

set_property(GLOBAL APPEND PROPERTY FORMATTED ${CMAKE_CURRENT_SOURCE_DIR}/common.h)

set(TARGETS)
foreach(src ${SOURCES})
    string(REPLACE ".c" "" name "${src}")
    string(REPLACE "/" "." target "tests.${name}")
    add_executable(${target} ${src})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${target} PRIVATE ttdal)
    add_test(NAME ${name} COMMAND ${target})
    list(APPEND TARGETS ${target})
    set_property(GLOBAL APPEND PROPERTY FORMATTED ${CMAKE_CURRENT_SOURCE_DIR}/${src})
endforeach()

# Labels
foreach(src ${SOURCES})
    string(REPLACE ".c" "" name "${src}")
    file(STRINGS ${src} marked REGEX "(#|//|/\\*|\\*).*MARK:")
    if(marked)
        list(GET marked 0 marked)
        string(REGEX MATCH "MARK:[ \t]*([^*\r\n]*)" _ "${marked}")
        string(REPLACE "," ";" labels "${CMAKE_MATCH_1}")
        set(cleaned)
        foreach(label ${labels})
            string(STRIP "${label}" label)
            if(label)
                list(APPEND cleaned "${label}")
            endif()
        endforeach()
        set_tests_properties(${name} PROPERTIES LABELS "${cleaned}")
    endif()
endforeach()

add_custom_target(tests DEPENDS ${TARGETS})
