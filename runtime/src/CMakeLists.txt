#
# runtime library cmake file
#

cmake_minimum_required(VERSION 2.8.0)
project(runtime)

find_package(Threads)

if(NOT ${CMAKE_USE_PTHREADS_INIT})
    message(FATAL_ERROR "Could not find POSIX Threads")
endif()

file(GLOB RUNTIME_SOURCES "*.c")

add_library(runtime STATIC ${RUNTIME_SOURCES})

target_include_directories(runtime PUBLIC "../include/")
target_link_libraries(runtime ${CMAKE_THREAD_LIBS_INIT})

# install

install(TARGETS runtime ARCHIVE DESTINATION "lib/")

