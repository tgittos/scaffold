/*
 * test_tool_param_dsl.c - Unit tests for tool parameter DSL
 */

#include "../unity/unity.h"
#include "../../src/tools/tool_param_dsl.h"
#include <stdlib.h>
#include <string.h>

static ToolRegistry registry;

void setUp(void) {
    init_tool_registry(&registry);
}

void tearDown(void) {
    cleanup_tool_registry(&registry);
}

/* Helper to find a tool by name in the registry */
static ToolFunction *find_tool(ToolRegistry *reg, const char *name) {
    for (size_t i = 0; i < reg->functions.count; i++) {
        if (strcmp(reg->functions.data[i].name, name) == 0) {
            return &reg->functions.data[i];
        }
    }
    return NULL;
}

/* Dummy execute function for testing */
static int dummy_execute(const ToolCall *call, ToolResult *result) {
    (void)call;
    result->result = strdup("{\"status\": \"ok\"}");
    result->success = 1;
    return 0;
}

/* Test count_enum_values with NULL */
void test_count_enum_values_null(void) {
    TEST_ASSERT_EQUAL_INT(0, count_enum_values(NULL));
}

/* Test count_enum_values with values */
void test_count_enum_values_with_values(void) {
    const char *values[] = {"one", "two", "three", NULL};
    TEST_ASSERT_EQUAL_INT(3, count_enum_values(values));
}

/* Test count_enum_values with empty array */
void test_count_enum_values_empty(void) {
    const char *values[] = {NULL};
    TEST_ASSERT_EQUAL_INT(0, count_enum_values(values));
}

/* Test registering a tool with no parameters */
void test_register_tool_no_params(void) {
    ToolDef def = {
        .name = "test_no_params",
        .description = "A tool with no parameters",
        .params = NULL,
        .param_count = 0,
        .execute = dummy_execute};

    int result = register_tool_from_def(&registry, &def);
    TEST_ASSERT_EQUAL_INT(0, result);

    /* Verify tool is registered */
    ToolFunction *tool = find_tool(&registry, "test_no_params");
    TEST_ASSERT_NOT_NULL(tool);
    TEST_ASSERT_EQUAL_STRING("test_no_params", tool->name);
    TEST_ASSERT_EQUAL_INT(0, tool->parameter_count);
}

/* Test registering a tool with parameters */
void test_register_tool_with_params(void) {
    static const ParamDef params[] = {
        {"name", "string", "The user name", NULL, 1},
        {"age", "number", "The user age", NULL, 0},
    };

    ToolDef def = {
        .name = "test_with_params",
        .description = "A tool with parameters",
        .params = params,
        .param_count = 2,
        .execute = dummy_execute};

    int result = register_tool_from_def(&registry, &def);
    TEST_ASSERT_EQUAL_INT(0, result);

    ToolFunction *tool = find_tool(&registry, "test_with_params");
    TEST_ASSERT_NOT_NULL(tool);
    TEST_ASSERT_EQUAL_INT(2, tool->parameter_count);
    TEST_ASSERT_EQUAL_STRING("name", tool->parameters[0].name);
    TEST_ASSERT_EQUAL_STRING("string", tool->parameters[0].type);
    TEST_ASSERT_EQUAL_INT(1, tool->parameters[0].required);
    TEST_ASSERT_EQUAL_STRING("age", tool->parameters[1].name);
    TEST_ASSERT_EQUAL_STRING("number", tool->parameters[1].type);
    TEST_ASSERT_EQUAL_INT(0, tool->parameters[1].required);
}

/* Test registering a tool with enum parameters */
void test_register_tool_with_enum(void) {
    static const char *color_values[] = {"red", "green", "blue", NULL};
    static const ParamDef params[] = {
        {"color", "string", "Choose a color", color_values, 1},
    };

    ToolDef def = {
        .name = "test_with_enum",
        .description = "A tool with enum parameter",
        .params = params,
        .param_count = 1,
        .execute = dummy_execute};

    int result = register_tool_from_def(&registry, &def);
    TEST_ASSERT_EQUAL_INT(0, result);

    ToolFunction *tool = find_tool(&registry, "test_with_enum");
    TEST_ASSERT_NOT_NULL(tool);
    TEST_ASSERT_EQUAL_INT(1, tool->parameter_count);
    TEST_ASSERT_EQUAL_INT(3, tool->parameters[0].enum_count);
    TEST_ASSERT_NOT_NULL(tool->parameters[0].enum_values);
    TEST_ASSERT_EQUAL_STRING("red", tool->parameters[0].enum_values[0]);
    TEST_ASSERT_EQUAL_STRING("green", tool->parameters[0].enum_values[1]);
    TEST_ASSERT_EQUAL_STRING("blue", tool->parameters[0].enum_values[2]);
}

/* Test register_tools_from_defs with multiple tools */
void test_register_multiple_tools(void) {
    static const ParamDef tool1_params[] = {
        {"x", "number", "X coordinate", NULL, 1},
        {"y", "number", "Y coordinate", NULL, 1},
    };

    static const ParamDef tool2_params[] = {
        {"text", "string", "Input text", NULL, 1},
    };

    static const ToolDef defs[] = {
        {"tool_one", "First tool", tool1_params, 2, dummy_execute},
        {"tool_two", "Second tool", tool2_params, 1, dummy_execute},
        {"tool_three", "Third tool (no params)", NULL, 0, dummy_execute},
    };

    int registered = register_tools_from_defs(&registry, defs, 3);
    TEST_ASSERT_EQUAL_INT(3, registered);

    TEST_ASSERT_NOT_NULL(find_tool(&registry, "tool_one"));
    TEST_ASSERT_NOT_NULL(find_tool(&registry, "tool_two"));
    TEST_ASSERT_NOT_NULL(find_tool(&registry, "tool_three"));
}

/* Test register_tool_from_def with NULL registry */
void test_register_null_registry(void) {
    ToolDef def = {
        .name = "test",
        .description = "Test",
        .params = NULL,
        .param_count = 0,
        .execute = dummy_execute};

    int result = register_tool_from_def(NULL, &def);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

/* Test register_tool_from_def with NULL def */
void test_register_null_def(void) {
    int result = register_tool_from_def(&registry, NULL);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

/* Test register_tool_from_def with NULL execute function */
void test_register_null_execute(void) {
    ToolDef def = {
        .name = "test",
        .description = "Test",
        .params = NULL,
        .param_count = 0,
        .execute = NULL};

    int result = register_tool_from_def(&registry, &def);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

/* Test register_tools_from_defs with NULL defs */
void test_register_multiple_null_defs(void) {
    int result = register_tools_from_defs(&registry, NULL, 5);
    TEST_ASSERT_EQUAL_INT(0, result);
}

/* Test register_tools_from_defs with zero count */
void test_register_multiple_zero_count(void) {
    static const ToolDef defs[] = {
        {"tool", "Test", NULL, 0, dummy_execute},
    };

    int result = register_tools_from_defs(&registry, defs, 0);
    TEST_ASSERT_EQUAL_INT(0, result);
}

/* Test that parameter strings are copied (not shared) */
void test_params_are_copied(void) {
    /* Use heap-allocated strings to avoid valgrind warnings about
     * optimized strlen reading beyond stack-allocated char arrays */
    char *name_buf = strdup("param_name");
    char *type_buf = strdup("string");
    char *desc_buf = strdup("Description");

    ParamDef params[1];
    memset(params, 0, sizeof(params));
    params[0].name = name_buf;
    params[0].type = type_buf;
    params[0].description = desc_buf;
    params[0].enum_values = NULL;
    params[0].required = 1;

    ToolDef def = {
        .name = "test_copy",
        .description = "Test copy",
        .params = params,
        .param_count = 1,
        .execute = dummy_execute};

    int result = register_tool_from_def(&registry, &def);
    TEST_ASSERT_EQUAL_INT(0, result);

    /* Modify original buffers */
    name_buf[0] = 'X';
    type_buf[0] = 'X';
    desc_buf[0] = 'X';

    /* Registered tool should have original values */
    ToolFunction *tool = find_tool(&registry, "test_copy");
    TEST_ASSERT_NOT_NULL(tool);
    TEST_ASSERT_EQUAL_STRING("param_name", tool->parameters[0].name);
    TEST_ASSERT_EQUAL_STRING("string", tool->parameters[0].type);
    TEST_ASSERT_EQUAL_STRING("Description", tool->parameters[0].description);

    /* Clean up heap allocations */
    free(name_buf);
    free(type_buf);
    free(desc_buf);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_count_enum_values_null);
    RUN_TEST(test_count_enum_values_with_values);
    RUN_TEST(test_count_enum_values_empty);
    RUN_TEST(test_register_tool_no_params);
    RUN_TEST(test_register_tool_with_params);
    RUN_TEST(test_register_tool_with_enum);
    RUN_TEST(test_register_multiple_tools);
    RUN_TEST(test_register_null_registry);
    RUN_TEST(test_register_null_def);
    RUN_TEST(test_register_null_execute);
    RUN_TEST(test_register_multiple_null_defs);
    RUN_TEST(test_register_multiple_zero_count);
    RUN_TEST(test_params_are_copied);

    return UNITY_END();
}
