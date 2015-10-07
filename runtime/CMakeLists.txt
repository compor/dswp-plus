#
# runtime sub-project cmake file
#

cmake_minimum_required(VERSION 2.8.0)
project(runtime-all)

add_subdirectory(src)
add_subdirectory(tests)

add_dependencies(runtime-tests runtime)

