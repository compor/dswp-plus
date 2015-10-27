#
# dswp library cmake file
#

cmake_minimum_required(VERSION 2.8.0)
project(dswp)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -g -O0")

find_package(LLVM REQUIRED CONFIG)

if(${LLVM_FOUND})
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
else()
    message(FATAL_ERROR "Could not find LLVM")
endif()

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

file(GLOB DSWP_SOURCES *.cpp)

add_library(dswp MODULE "${DSWP_SOURCES}")

target_include_directories(dswp PUBLIC "../include/")

llvm_map_components_to_libnames(llvm_libs support core passes)
target_link_libraries(dswp ${llvm_libs})

# install

install(TARGETS dswp LIBRARY DESTINATION "lib/")

