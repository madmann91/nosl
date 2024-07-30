cmake_minimum_required(VERSION 3.16)
project(nosl VERSION 0.0.1)

option(BUILD_SHARED_LIBS "Build a shared library." ON)

set(CMAKE_C_STANDARD 23)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_compile_options(
    $<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Debug,Coverage>>:-fsanitize=address>
    $<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Debug,Coverage>>:-fsanitize=undefined>
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall>
    $<$<C_COMPILER_ID:GNU,Clang>:-Wextra>
    $<$<C_COMPILER_ID:GNU,Clang>:-pedantic>)

add_link_options(
    $<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Debug,Coverage>>:-fsanitize=address>
    $<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Debug,Coverage>>:-fsanitize=undefined>)

include(contrib/overture/cmake/Coverage.cmake)

add_subdirectory(contrib)
add_subdirectory(src)
