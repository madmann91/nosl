if (BUILD_SHARED_LIBS)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

set(BUILD_SHARED_LIBS OFF)
add_subdirectory(overture)
