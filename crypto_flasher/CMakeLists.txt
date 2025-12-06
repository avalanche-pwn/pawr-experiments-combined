#-------------------------------------------------------------------------------
# Zephyr Example Application
#
# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)


function(find_keys_json result_var)
    # Initialize the path variable
    set(current_dir ${CMAKE_CURRENT_SOURCE_DIR})

    # Loop to go upwards until the root directory is reached or file is found
    while(NOT EXISTS "${current_dir}/keys.json")
        # Go one directory up
        get_filename_component(current_dir ${current_dir} DIRECTORY)
        
        # If we reach the root directory (empty current_dir), exit
        if (current_dir STREQUAL "")
            set(${result_var} "" PARENT_SCOPE)  # If not found, set to empty
            return()
        endif()
    endwhile()

    # When found, set the result variable
    set(${result_var} "${current_dir}/keys.json" PARENT_SCOPE)
endfunction()
find_keys_json(KEYS_JSON_PATH)

if(NOT KEYS_JSON_PATH)
    message(FATAL_ERROR "keys.json not found in parent directories.")
else()
    message(STATUS "Found keys.json at: ${KEYS_JSON_PATH}")
endif()

add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/src/keys.h
    COMMAND west keymgr --cmake-dump ${CMAKE_SOURCE_DIR}/src/keys.h
    DEPENDS ${KEYS_JSON_PATH}
    COMMENT "Dumping device keys into header file"
)

add_custom_target(generate_keys DEPENDS ${CMAKE_SOURCE_DIR}/src/keys.h)
target_sources(app PRIVATE src/main.c)
add_dependencies(app generate_keys)
