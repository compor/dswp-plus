#
# runtime tests cmake file
#

cmake_minimum_required(VERSION 2.8.0)
project(runtime-tests)

add_custom_target(runtime-tests)

file(GLOB RUNTIME_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.c")

foreach(RUNTIME_SOURCE_FILE ${RUNTIME_SOURCES})
    string(REGEX REPLACE "[.]c$" "" RUNTIME_BIN ${RUNTIME_SOURCE_FILE})
    add_executable(${RUNTIME_BIN} ${RUNTIME_SOURCE_FILE})

    message(STATUS "generating target ${RUNTIME_SOURCE_FILE} from file \
        ${RUNTIME_SOURCE_FILE}")

    target_include_directories(${RUNTIME_BIN} PUBLIC "../include/")
    target_link_libraries(${RUNTIME_BIN} runtime)

    add_dependencies(runtime-tests ${RUNTIME_BIN})
    add_dependencies(${RUNTIME_BIN} runtime)
endforeach(RUNTIME_SOURCE_FILE)


