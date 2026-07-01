cmake_minimum_required(VERSION 3.16)
project(nosl VERSION 0.0.1)

option(BUILD_SHARED_LIBS "Builds a shared library." ON)

set(CMAKE_C_STANDARD 23)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_compile_options(
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall>
    $<$<C_COMPILER_ID:GNU,Clang>:-Wextra>
    $<$<C_COMPILER_ID:GNU,Clang>:-pedantic>)

if (PROJECT_IS_TOP_LEVEL)
    option(NOSL_ENABLE_COVERAGE          "Enables code coverage build type and target." OFF)
    option(NOSL_ENABLE_ADDRESS_SANITIZER "Enables the address sanitizer." OFF)
    option(NOSL_ENABLE_UNDEF_SANITIZER   "Enables the undefined value sanitizer." OFF)

    if (NOSL_ENABLE_ADDRESS_SANITIZER)
        add_compile_options($<$<C_COMPILER_ID:GNU,Clang>:-fsanitize=address>)
        add_link_options($<$<C_COMPILER_ID:GNU,Clang>:-fsanitize=address>)
    endif()

    if (NOSL_ENABLE_UNDEF_SANITIZER)
        add_compile_options($<$<C_COMPILER_ID:GNU,Clang>:-fsanitize=undefined>)
        add_link_options($<$<C_COMPILER_ID:GNU,Clang>:-fsanitize=undefined>)
    endif()

    if (NOSL_ENABLE_COVERAGE)
        include(contrib/overture/cmake/Coverage.cmake)
    endif()
endif()

add_subdirectory(contrib)
add_subdirectory(src)

if (PROJECT_IS_TOP_LEVEL)
    include(CTest)
    if (BUILD_TESTING)
        add_subdirectory(test)
    endif()
endif()
