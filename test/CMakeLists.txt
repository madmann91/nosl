# Adds a NOSL test that will be run with CTest.
# Parameters:
#   NAME       - Test name.
#   FILEPATH   - Path to file that gets passed to noslc.
#   PASS_REGEX - Regular expression that makes the test pass, overrides return code. See the
#                PASS_REGULAR_EXPRESSION property on tests.
function (add_nosl_test)
    cmake_parse_arguments(add_nosl_test_arg "WILL_FAIL" "FILE;PASS_REGEX" "" ${ARGN})
    if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${add_nosl_test_arg_FILE})
        message(FATAL_ERROR "Test cannot run because ${add_nosl_test_arg_FILE} is missing")
    endif()
    add_test(
        NAME ${add_nosl_test_arg_FILE}
        COMMAND noslc ${add_nosl_test_arg_FILE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    if (NOT ${add_nosl_test_arg_PASS_REGEX} STREQUAL "")
        set_tests_properties(${add_nosl_test_arg_FILE} PROPERTIES PASS_REGULAR_EXPRESSION "${add_nosl_test_arg_PASS_REGEX}")
    endif()
endfunction()

# Preprocessor Tests ------------------------------------------------------------------------------

add_nosl_test(FILE "preprocessor/fail/double_else.osl"          PASS_REGEX "'#else' after '#else'")
add_nosl_test(FILE "preprocessor/fail/undef.osl"                PASS_REGEX "unknown macro")
add_nosl_test(FILE "preprocessor/fail/unterminated_if_0.osl"    PASS_REGEX "unterminated '#if'")
add_nosl_test(FILE "preprocessor/fail/unterminated_if_1.osl"    PASS_REGEX "unterminated '#if'")
add_nosl_test(FILE "preprocessor/fail/unterminated_if_if_0.osl" PASS_REGEX "unterminated '#if'")

add_nosl_test(FILE "preprocessor/pass/error.osl")
add_nosl_test(FILE "preprocessor/pass/warning.osl")
add_nosl_test(FILE "preprocessor/pass/line.osl")
add_nosl_test(FILE "preprocessor/pass/pragma.osl")
add_nosl_test(FILE "preprocessor/pass/undef.osl")

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

add_nosl_test(FILE "frontend/fail/missing_default_value.osl" PASS_REGEX "expected '='")
add_nosl_test(FILE "frontend/fail/invalid_break.osl"         PASS_REGEX "'break' is not allowed outside of loops")
add_nosl_test(FILE "frontend/fail/invalid_continue.osl"      PASS_REGEX "'continue' is not allowed outside of loops")
add_nosl_test(FILE "frontend/fail/shader_redefinition.osl"   PASS_REGEX "redefinition for shader")
add_nosl_test(FILE "frontend/fail/function_redefinition.osl" PASS_REGEX "redefinition for function")
add_nosl_test(FILE "frontend/fail/unsized_array.osl"         PASS_REGEX "unsized arrays are only allowed as function or shader parameters")

add_nosl_test(FILE "frontend/fail/invalid_casts.osl"
    PASS_REGEX "\
invalid cast from type 'string' to type 'float'.*\
invalid cast from type 'string' to type 'int'.*\
invalid cast from type 'string' to type 'vector'.*\
invalid cast from type 'string' to type 'point'.*\
invalid cast from type 'string' to type 'normal'.*\
invalid cast from type 'string' to type 'color'.*\
invalid cast from type 'string' to type 'matrix'.*\
expected type 'float\\[4\\]', but got type 'float\\[\\]'")
