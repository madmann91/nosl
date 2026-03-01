cmake_minimum_required(VERSION 3.16)
project(nosl VERSION 0.0.1)

option(BUILD_SHARED_LIBS "Build a shared library." ON)
option(ENABLE_ADDRESS_SANITIZER "Enables the address sanitizer" OFF)
option(ENABLE_UNDEF_SANITIZER "Enables the undefined value sanitizer" OFF)

set(CMAKE_C_STANDARD 23)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_compile_options(
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall>
    $<$<C_COMPILER_ID:GNU,Clang>:-Wextra>
    $<$<C_COMPILER_ID:GNU,Clang>:-pedantic>)

if (ENABLE_ADDRESS_SANITIZER)
    add_compile_options($<$<C_COMPILER_ID:GNU,Clang>:-fsanitize=address>)
    add_link_options($<$<C_COMPILER_ID:GNU,Clang>:-fsanitize=address>)
endif()

if (ENABLE_UNDEF_SANITIZER)
    add_compile_options($<$<C_COMPILER_ID:GNU,Clang>:-fsanitize=undefined>)
    add_link_options($<$<C_COMPILER_ID:GNU,Clang>:-fsanitize=undefined>)
endif()

include(contrib/overture/cmake/Coverage.cmake)

add_subdirectory(contrib)
add_subdirectory(src)

include(CTest)
if (BUILD_TESTING)
    add_subdirectory(test)
endif()
