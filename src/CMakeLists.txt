add_library(libnosl
    parse.c
    lexer.c
    token.c
    ast.c
    type.c
    type_table.c
    env.c
    check.c
    preprocessor.c)
target_compile_definitions(libnosl PUBLIC
    -DNOSL_VERSION_MAJOR=${CMAKE_PROJECT_VERSION_MAJOR}
    -DNOSL_VERSION_MINOR=${CMAKE_PROJECT_VERSION_MINOR}
    -DNOSL_VERSION_PATCH=${CMAKE_PROJECT_VERSION_PATCH})
set_target_properties(libnosl PROPERTIES PREFIX "")
target_link_libraries(libnosl PUBLIC
    overture
    overture_log
    overture_mem_pool
    overture_str_pool
    overture_file)

# Build a version of noslc without builtins.
add_executable(noslc_without_builtins main.c)
target_link_libraries(noslc_without_builtins PRIVATE libnosl)

# Preprocess builtins using the version of noslc above.
# FIXME: Currently, CMake does not allow setting the search paths for #embed, so we need to generate
# the file in the current source directory.
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/builtins.osl.preprocessed
    DEPENDS noslc_without_builtins builtins.osl
    COMMAND noslc_without_builtins builtins.osl --print-ast > builtins.osl.preprocessed
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_custom_target(generate_builtins DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/builtins.osl.preprocessed)

# Build the final version of noslc, with builtins embedded as a string.
add_executable(noslc main.c)
add_dependencies(noslc generate_builtins)
target_compile_definitions(noslc PRIVATE -DENABLE_BUILTINS)
target_link_libraries(noslc PRIVATE libnosl)
