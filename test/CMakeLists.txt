# Adds a NOSL test that will be run with CTest.
# Parameters:
#   NAME     - Test name.
#   FILEPATH - Path to file that gets passed to noslc.
#   ARGS     - Additional arguments to pass to noslc.
#   REGEX    - Regular expression that makes the test pass, overrides return code. See the
#              PASS_REGULAR_EXPRESSION property on tests.
function (add_nosl_test)
    cmake_parse_arguments(add_nosl_test_arg "WILL_FAIL" "FILE;REGEX" "ARGS" ${ARGN})
    if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${add_nosl_test_arg_FILE})
        message(FATAL_ERROR "Test cannot run because ${add_nosl_test_arg_FILE} is missing")
    endif()
    add_test(
        NAME ${add_nosl_test_arg_FILE}
        COMMAND noslc ${add_nosl_test_arg_FILE} ${add_nosl_test_ARGS}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    if (NOT ${add_nosl_test_arg_REGEX} STREQUAL "")
        set_tests_properties(${add_nosl_test_arg_FILE} PROPERTIES PASS_REGULAR_EXPRESSION "${add_nosl_test_arg_REGEX}")
    endif()
endfunction()

# Preprocessor Tests ------------------------------------------------------------------------------

add_nosl_test(FILE "preprocessor/fail/double_else.osl"          REGEX "'#else' after '#else'")
add_nosl_test(FILE "preprocessor/fail/undef.osl"                REGEX "unknown macro")
add_nosl_test(FILE "preprocessor/fail/unterminated_if_0.osl"    REGEX "unterminated '#if'")
add_nosl_test(FILE "preprocessor/fail/unterminated_if_1.osl"    REGEX "unterminated '#if'")
add_nosl_test(FILE "preprocessor/fail/unterminated_if_if_0.osl" REGEX "unterminated '#if'")
add_nosl_test(FILE "preprocessor/fail/macro_redefinition.osl"   REGEX "redefinition for macro 'FOO'" ARGS --warns-as-errors)
add_nosl_test(FILE "preprocessor/fail/error.osl"                REGEX "This should produce an error")
add_nosl_test(FILE "preprocessor/fail/warning.osl"              REGEX "This should produce a warning" ARGS --warns-as-errors)

add_nosl_test(FILE "preprocessor/pass/if_elif_else.osl")
add_nosl_test(FILE "preprocessor/pass/warning.osl")
add_nosl_test(FILE "preprocessor/pass/line.osl")
add_nosl_test(FILE "preprocessor/pass/pragma.osl")
add_nosl_test(FILE "preprocessor/pass/undef.osl")

# Tests that need macro expansion to work
#add_nosl_test(FILE "preprocessor/pass/define.osl")

# Frontend Tests ----------------------------------------------------------------------------------

add_nosl_test(FILE "frontend/pass/functions.osl")
add_nosl_test(FILE "frontend/pass/types.osl")
add_nosl_test(FILE "frontend/pass/attributes.osl")
add_nosl_test(FILE "frontend/pass/function_overloads.osl")
add_nosl_test(FILE "frontend/pass/conditions.osl")
add_nosl_test(FILE "frontend/pass/ternary_operator.osl")
add_nosl_test(FILE "frontend/pass/explicit_casts.osl")
add_nosl_test(FILE "frontend/pass/implicit_casts.osl")
add_nosl_test(FILE "frontend/pass/arrays.osl")

# Tests that need the builtin library to work
#add_nosl_test(FILE "frontend/pass/loops.osl")
#add_nosl_test(FILE "frontend/pass/nested_functions.osl")

add_nosl_test(FILE "frontend/fail/missing_default_value.osl" REGEX "expected '='")
add_nosl_test(FILE "frontend/fail/invalid_break.osl"         REGEX "'break' is not allowed outside of loops")
add_nosl_test(FILE "frontend/fail/invalid_continue.osl"      REGEX "'continue' is not allowed outside of loops")
add_nosl_test(FILE "frontend/fail/shader_redefinition.osl"   REGEX "redefinition for shader")
add_nosl_test(FILE "frontend/fail/function_redefinition.osl" REGEX "redefinition for function")
add_nosl_test(FILE "frontend/fail/unsized_array.osl"         REGEX "unsized arrays are only allowed as function or shader parameters")

add_nosl_test(FILE "frontend/fail/invalid_casts.osl"
    REGEX "\
invalid cast from type 'string' to type 'float'.*\
invalid cast from type 'string' to type 'int'.*\
invalid cast from type 'string' to type 'vector'.*\
invalid cast from type 'string' to type 'point'.*\
invalid cast from type 'string' to type 'normal'.*\
invalid cast from type 'string' to type 'color'.*\
invalid cast from type 'string' to type 'matrix'.*\
expected type 'float\\[4\\]', but got type 'float\\[\\]'")
