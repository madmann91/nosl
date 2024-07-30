add_library(libnosl parse.c lexer.c token.c ast.c)
set_target_properties(libnosl PROPERTIES PREFIX "")
target_link_libraries(libnosl PUBLIC overture overture_log overture_mem_pool)

add_executable(noslc main.c)
target_link_libraries(noslc PUBLIC libnosl)
