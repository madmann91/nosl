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

add_nosl_test(FILE "preprocessor/pass/define.osl")

# Frontend Tests ----------------------------------------------------------------------------------

add_nosl_test(FILE "frontend/pass/functions.osl")
add_nosl_test(FILE "frontend/pass/types.osl")
add_nosl_test(FILE "frontend/pass/attributes.osl")
add_nosl_test(FILE "frontend/pass/function_overloads.osl")
add_nosl_test(FILE "frontend/pass/conditions.osl")
add_nosl_test(FILE "frontend/pass/ternary_operator.osl")
add_nosl_test(FILE "frontend/pass/explicit_casts.osl")
add_nosl_test(FILE "frontend/pass/implicit_casts.osl")
add_nosl_test(FILE "frontend/pass/struct_fields.osl")
add_nosl_test(FILE "frontend/pass/arrays.osl")
add_nosl_test(FILE "frontend/pass/nested_functions.osl")
add_nosl_test(FILE "frontend/pass/index_expression.osl")
add_nosl_test(FILE "frontend/pass/components.osl")

# Tests that need the builtin library to work
#add_nosl_test(FILE "frontend/pass/loops.osl")

add_nosl_test(FILE "frontend/fail/missing_default_value.osl"    REGEX "expected '='")
add_nosl_test(FILE "frontend/fail/invalid_break.osl"            REGEX "'break' is not allowed outside of loops")
add_nosl_test(FILE "frontend/fail/invalid_continue.osl"         REGEX "'continue' is not allowed outside of loops")
add_nosl_test(FILE "frontend/fail/shader_redefinition.osl"      REGEX "redefinition for shader")
add_nosl_test(FILE "frontend/fail/function_redefinition.osl"    REGEX "redefinition for function")
add_nosl_test(FILE "frontend/fail/variable_redefinition.osl"    REGEX "redefinition for symbol 'x'")
add_nosl_test(FILE "frontend/fail/unsized_array.osl"            REGEX "unsized arrays are only allowed as function or shader parameters")
add_nosl_test(FILE "frontend/fail/no_viable_candidate.osl"      REGEX "no viable candidate for call to 'foo'")
add_nosl_test(FILE "frontend/fail/ambiguous_call.osl"           REGEX "ambiguous call to 'foo'")
add_nosl_test(FILE "frontend/fail/too_many_fields.osl"          REGEX "expected 2 initializer\\(s\\) for type 'S', but got 3")
add_nosl_test(FILE "frontend/fail/missing_initializer.osl"      REGEX "missing initializer for field 'c' in type 'S'")
add_nosl_test(FILE "frontend/fail/shadow_symbol.osl"            REGEX "symbol 'x' shadows previous definition" ARGS --warns-as-errors)
add_nosl_test(FILE "frontend/fail/incomplete_coercion.osl"      REGEX "missing initializer for field 'c' in type 'S'" ARGS --warns-as-errors)
add_nosl_test(FILE "frontend/fail/assign_value.osl"             REGEX "value cannot be written to")
add_nosl_test(FILE "frontend/fail/unknown_array_size.osl"       REGEX "array dimension must be constant and strictly positive")
add_nosl_test(FILE "frontend/fail/negative_array_size.osl"      REGEX "array dimension must be constant and strictly positive")
add_nosl_test(FILE "frontend/fail/missing_function_body.osl"    REGEX "missing function body")
add_nosl_test(FILE "frontend/fail/missing_return_value.osl"     REGEX "missing return value")
add_nosl_test(FILE "frontend/fail/overloaded_identifier.osl"    REGEX "cannot resolve overloaded identifier 'foo'")
add_nosl_test(FILE "frontend/fail/function_as_value.osl"        REGEX "cannot use function 'foo' as value")
add_nosl_test(FILE "frontend/fail/struct_as_value.osl"          REGEX "cannot use structure 'S' as value")
add_nosl_test(FILE "frontend/fail/operator_struct.osl"          REGEX "structure name '__operator__add__' is not allowed")
add_nosl_test(FILE "frontend/fail/non_indexable_expression.osl" REGEX "expected vector, point, normal, color, or array type, but got type 'int'")

add_nosl_test(FILE "frontend/fail/unknown_identifiers.osl"
    REGEX "\
unknown identifier 'T'.*\
unknown identifier 'i'")

add_nosl_test(FILE "frontend/fail/void_variable.osl"
    REGEX "\
expected parameter type, but got type 'void'.*\
expected variable type, but got type 'void'")

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
