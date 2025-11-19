add_library(libnosl
    parse.c
    lexer.c
    token.c
    ast.c
    type.c
    type_table.c
    env.c
    check.c
    preprocessor.c
    builtins_stubs.c)
set_target_properties(libnosl PROPERTIES PREFIX "")
target_link_libraries(libnosl PUBLIC
    overture
    overture_log
    overture_mem_pool
    overture_str_pool
    overture_file)

add_executable(noslc main.c)
target_link_libraries(noslc PUBLIC libnosl)
