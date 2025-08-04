#include "unity.h"
#include "file_tools.h"
#include "tools_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

void setUp(void) {}
void tearDown(void) {}

void test_register_file_tools(void) {
    ToolRegistry registry;
    init_tool_registry(&registry);
    
    int result = register_file_tools(&registry);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(7, registry.function_count);
    
    TEST_ASSERT_NOT_NULL(registry.functions);
    TEST_ASSERT_EQUAL_STRING("file_read", registry.functions[0].name);
    TEST_ASSERT_EQUAL_STRING("file_write", registry.functions[1].name);
    TEST_ASSERT_EQUAL_STRING("file_append", registry.functions[2].name);
    TEST_ASSERT_EQUAL_STRING("file_list", registry.functions[3].name);
    TEST_ASSERT_EQUAL_STRING("file_search", registry.functions[4].name);
    TEST_ASSERT_EQUAL_STRING("file_info", registry.functions[5].name);
    TEST_ASSERT_EQUAL_STRING("file_delta", registry.functions[6].name);
    
    cleanup_tool_registry(&registry);
}

void test_file_read_content_valid_file(void) {
    FILE* test_file = fopen("test_read.txt", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "Hello, World!\nThis is a test file.");
    fclose(test_file);
    
    char* content = NULL;
    FileErrorCode result = file_read_content("test_read.txt", 1, -1, &content);
    
    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, result);
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_TRUE(strstr(content, "Hello, World!") != NULL);
    TEST_ASSERT_TRUE(strstr(content, "This is a test file.") != NULL);
    
    free(content);
    unlink("test_read.txt");
}

void test_file_read_content_nonexistent_file(void) {
    char* content = NULL;
    FileErrorCode result = file_read_content("nonexistent_file.txt", 1, -1, &content);
    
    TEST_ASSERT_EQUAL_INT(FILE_ERROR_NOT_FOUND, result);
    TEST_ASSERT_NULL(content);
}

void test_file_read_content_null_parameters(void) {
    char* content = NULL;
    FileErrorCode result = file_read_content(NULL, 1, -1, &content);
    TEST_ASSERT_EQUAL_INT(FILE_ERROR_INVALID_PATH, result);
    
    result = file_read_content("test.txt", 1, -1, NULL);
    TEST_ASSERT_EQUAL_INT(FILE_ERROR_INVALID_PATH, result);
}

void test_file_write_content_basic(void) {
    const char* content = "Test content for writing\nSecond line";
    FileErrorCode result = file_write_content("test_write.txt", content, 0);
    
    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, result);
    
    FILE* file = fopen("test_write.txt", "r");
    TEST_ASSERT_NOT_NULL(file);
    char buffer[256] = {0};
    fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    
    TEST_ASSERT_EQUAL_STRING(content, buffer);
    
    unlink("test_write.txt");
}

void test_file_write_content_null_parameters(void) {
    FileErrorCode result = file_write_content(NULL, "content", 0);
    TEST_ASSERT_EQUAL_INT(FILE_ERROR_INVALID_PATH, result);
    
    result = file_write_content("test.txt", NULL, 0);
    TEST_ASSERT_EQUAL_INT(FILE_ERROR_INVALID_PATH, result);
}

void test_file_append_content_basic(void) {
    FILE* test_file = fopen("test_append.txt", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "Initial content");
    fclose(test_file);
    
    const char* append_content = "\nAppended content";
    FileErrorCode result = file_append_content("test_append.txt", append_content);
    
    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, result);
    
    char* final_content = NULL;
    result = file_read_content("test_append.txt", 1, -1, &final_content);
    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, result);
    TEST_ASSERT_NOT_NULL(final_content);
    TEST_ASSERT_TRUE(strstr(final_content, "Initial content") != NULL);
    TEST_ASSERT_TRUE(strstr(final_content, "Appended content") != NULL);
    
    free(final_content);
    unlink("test_append.txt");
}

void test_file_list_directory_basic(void) {
    mkdir("test_dir", 0755);
    
    FILE* file1 = fopen("test_dir/file1.txt", "w");
    TEST_ASSERT_NOT_NULL(file1);
    fprintf(file1, "content1");
    fclose(file1);
    
    FILE* file2 = fopen("test_dir/file2.txt", "w");
    TEST_ASSERT_NOT_NULL(file2);
    fprintf(file2, "content2");
    fclose(file2);
    
    DirectoryListing listing = {0};
    FileErrorCode result = file_list_directory("test_dir", NULL, 0, 0, &listing);
    
    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, result);
    TEST_ASSERT_TRUE(listing.count >= 2);
    TEST_ASSERT_NOT_NULL(listing.entries);
    
    int found_file1 = 0, found_file2 = 0;
    for (int i = 0; i < listing.count; i++) {
        if (strcmp(listing.entries[i].name, "file1.txt") == 0) {
            found_file1 = 1;
            TEST_ASSERT_EQUAL_INT(0, listing.entries[i].is_directory);
        }
        if (strcmp(listing.entries[i].name, "file2.txt") == 0) {
            found_file2 = 1;
            TEST_ASSERT_EQUAL_INT(0, listing.entries[i].is_directory);
        }
    }
    
    TEST_ASSERT_TRUE(found_file1);
    TEST_ASSERT_TRUE(found_file2);
    
    cleanup_directory_listing(&listing);
    unlink("test_dir/file1.txt");
    unlink("test_dir/file2.txt");
    rmdir("test_dir");
}

void test_file_search_content_basic(void) {
    mkdir("search_test", 0755);
    
    FILE* file1 = fopen("search_test/test1.txt", "w");
    TEST_ASSERT_NOT_NULL(file1);
    fprintf(file1, "This file contains the search pattern");
    fclose(file1);
    
    FILE* file2 = fopen("search_test/test2.txt", "w");
    TEST_ASSERT_NOT_NULL(file2);
    fprintf(file2, "This file does not contain it");
    fclose(file2);
    
    SearchResults results = {0};
    FileErrorCode result = file_search_content("search_test", "search pattern", NULL, 1, 1, &results);
    
    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, result);
    TEST_ASSERT_TRUE(results.count >= 1);
    TEST_ASSERT_NOT_NULL(results.results);
    
    int found_match = 0;
    for (int i = 0; i < results.count; i++) {
        if (strstr(results.results[i].file_path, "test1.txt") != NULL) {
            found_match = 1;
            TEST_ASSERT_TRUE(strstr(results.results[i].line_content, "search pattern") != NULL);
        }
    }
    TEST_ASSERT_TRUE(found_match);
    
    cleanup_search_results(&results);
    unlink("search_test/test1.txt");
    unlink("search_test/test2.txt");
    rmdir("search_test");
}

void test_file_get_info_basic(void) {
    FILE* test_file = fopen("info_test.txt", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "Test content for file info");
    fclose(test_file);
    
    FileInfo info = {0};
    FileErrorCode result = file_get_info("info_test.txt", &info);
    
    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, result);
    TEST_ASSERT_NOT_NULL(info.path);
    TEST_ASSERT_TRUE(info.size > 0);
    TEST_ASSERT_EQUAL_INT(0, info.is_directory);
    TEST_ASSERT_TRUE(info.permissions > 0);
    
    cleanup_file_info(&info);
    unlink("info_test.txt");
}

void test_execute_file_read_tool_call(void) {
    FILE* test_file = fopen("tool_test.txt", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "Tool test content");
    fclose(test_file);
    
    ToolCall call;
    call.id = strdup("test_call_1");
    call.name = strdup("file_read");
    call.arguments = strdup("{\"file_path\": \"tool_test.txt\"}");
    
    ToolResult result;
    int exec_result = execute_file_read_tool_call(&call, &result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_TRUE(strstr(result.result, "Tool test content") != NULL);
    
    free(call.id);
    free(call.name);
    free(call.arguments);
    free(result.tool_call_id);
    free(result.result);
    unlink("tool_test.txt");
}

void test_execute_file_write_tool_call(void) {
    ToolCall call;
    call.id = strdup("test_call_2");
    call.name = strdup("file_write");
    call.arguments = strdup("{\"file_path\": \"write_tool_test.txt\", \"content\": \"Written by tool\"}");
    
    ToolResult result;
    int exec_result = execute_file_write_tool_call(&call, &result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    TEST_ASSERT_NOT_NULL(result.result);
    
    char* file_content = NULL;
    FileErrorCode read_result = file_read_content("write_tool_test.txt", 1, -1, &file_content);
    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, read_result);
    TEST_ASSERT_TRUE(strstr(file_content, "Written by tool") != NULL);
    
    free(call.id);
    free(call.name);
    free(call.arguments);
    free(result.tool_call_id);
    free(result.result);
    free(file_content);
    unlink("write_tool_test.txt");
}

void test_split_lines_basic(void) {
    const char* content = "line1\nline2\nline3";
    int line_count;
    
    char** lines = split_lines(content, &line_count);
    
    TEST_ASSERT_EQUAL_INT(3, line_count);
    TEST_ASSERT_NOT_NULL(lines);
    TEST_ASSERT_EQUAL_STRING("line1", lines[0]);
    TEST_ASSERT_EQUAL_STRING("line2", lines[1]);
    TEST_ASSERT_EQUAL_STRING("line3", lines[2]);
    
    for (int i = 0; i < line_count; i++) {
        free(lines[i]);
    }
    free(lines);
}

void test_split_lines_empty_content(void) {
    const char* content = "";
    int line_count;
    
    char** lines = split_lines(content, &line_count);
    
    TEST_ASSERT_EQUAL_INT(0, line_count);
    TEST_ASSERT_NULL(lines);
}

void test_split_lines_single_line(void) {
    const char* content = "single line";
    int line_count;
    
    char** lines = split_lines(content, &line_count);
    
    TEST_ASSERT_EQUAL_INT(1, line_count);
    TEST_ASSERT_NOT_NULL(lines);
    TEST_ASSERT_EQUAL_STRING("single line", lines[0]);
    
    free(lines[0]);
    free(lines);
}

void test_join_lines_basic(void) {
    char* lines[] = {"line1", "line2", "line3"};
    char* result = join_lines(lines, 3);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("line1\nline2\nline3\n", result);
    
    free(result);
}

void test_file_apply_delta_insert(void) {
    // Create test file
    FILE* test_file = fopen("delta_test.txt", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "line1\nline2\nline3\n");
    fclose(test_file);
    
    // Create delta patch to insert at line 2
    DeltaPatch patch = {0};
    patch.operations = malloc(sizeof(DeltaOperation));
    patch.num_operations = 1;
    patch.create_backup = 0;
    
    DeltaOperation* op = &patch.operations[0];
    op->type = DELTA_INSERT;
    op->start_line = 2;
    op->line_count = 0;
    op->num_lines = 1;
    op->lines = malloc(sizeof(char*));
    op->lines[0] = strdup("inserted_line");
    op->context_before = NULL;
    op->context_after = NULL;
    
    // Apply delta
    FileErrorCode result = file_apply_delta("delta_test.txt", &patch);
    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, result);
    
    // Verify result
    char* content = NULL;
    FileErrorCode read_result = file_read_content("delta_test.txt", 0, 0, &content);
    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, read_result);
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_TRUE(strstr(content, "line1\ninserted_line\nline2\nline3") != NULL);
    
    // Cleanup
    free(content);
    cleanup_delta_patch(&patch);
    unlink("delta_test.txt");
}

void test_file_apply_delta_delete(void) {
    // Create test file
    FILE* test_file = fopen("delta_test2.txt", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "line1\nline2\nline3\nline4\n");
    fclose(test_file);
    
    // Create delta patch to delete line 2
    DeltaPatch patch = {0};
    patch.operations = malloc(sizeof(DeltaOperation));
    patch.num_operations = 1;
    patch.create_backup = 0;
    
    DeltaOperation* op = &patch.operations[0];
    op->type = DELTA_DELETE;
    op->start_line = 2;
    op->line_count = 1;
    op->num_lines = 0;
    op->lines = NULL;
    op->context_before = NULL;
    op->context_after = NULL;
    
    // Apply delta
    FileErrorCode result = file_apply_delta("delta_test2.txt", &patch);
    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, result);
    
    // Verify result
    char* content = NULL;
    FileErrorCode read_result = file_read_content("delta_test2.txt", 0, 0, &content);
    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, read_result);
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_TRUE(strstr(content, "line1\nline3\nline4") != NULL);
    TEST_ASSERT_NULL(strstr(content, "line2"));
    
    // Cleanup
    free(content);
    cleanup_delta_patch(&patch);
    unlink("delta_test2.txt");
}

void test_file_apply_delta_replace(void) {
    // Create test file
    FILE* test_file = fopen("delta_test3.txt", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    fprintf(test_file, "line1\nline2\nline3\n");
    fclose(test_file);
    
    // Create delta patch to replace line 2
    DeltaPatch patch = {0};
    patch.operations = malloc(sizeof(DeltaOperation));
    patch.num_operations = 1;
    patch.create_backup = 0;
    
    DeltaOperation* op = &patch.operations[0];
    op->type = DELTA_REPLACE;
    op->start_line = 2;
    op->line_count = 1;
    op->num_lines = 1;
    op->lines = malloc(sizeof(char*));
    op->lines[0] = strdup("replaced_line");
    op->context_before = NULL;
    op->context_after = NULL;
    
    // Apply delta
    FileErrorCode result = file_apply_delta("delta_test3.txt", &patch);
    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, result);
    
    // Verify result
    char* content = NULL;
    FileErrorCode read_result = file_read_content("delta_test3.txt", 0, 0, &content);
    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, read_result);
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_TRUE(strstr(content, "line1\nreplaced_line\nline3") != NULL);
    TEST_ASSERT_NULL(strstr(content, "line2"));
    
    // Cleanup
    free(content);
    cleanup_delta_patch(&patch);
    unlink("delta_test3.txt");
}

void test_execute_file_delta_tool_call(void) {
    ToolCall call;
    call.id = strdup("test_delta_call");
    call.name = strdup("file_delta");
    call.arguments = strdup("{\"file_path\": \"test.txt\", \"operations\": []}");
    
    ToolResult result;
    int exec_result = execute_file_delta_tool_call(&call, &result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_EQUAL_INT(0, result.success);  // Should fail since not fully implemented
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_TRUE(strstr(result.result, "implementation in progress") != NULL);
    
    free(call.id);
    free(call.name);
    free(call.arguments);
    free(result.tool_call_id);
    free(result.result);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_register_file_tools);
    RUN_TEST(test_file_read_content_valid_file);
    RUN_TEST(test_file_read_content_nonexistent_file);
    RUN_TEST(test_file_read_content_null_parameters);
    RUN_TEST(test_file_write_content_basic);
    RUN_TEST(test_file_write_content_null_parameters);
    RUN_TEST(test_file_append_content_basic);
    RUN_TEST(test_file_list_directory_basic);
    RUN_TEST(test_file_search_content_basic);
    RUN_TEST(test_file_get_info_basic);
    RUN_TEST(test_execute_file_read_tool_call);
    RUN_TEST(test_execute_file_write_tool_call);
    RUN_TEST(test_split_lines_basic);
    RUN_TEST(test_split_lines_empty_content);
    RUN_TEST(test_split_lines_single_line);
    RUN_TEST(test_join_lines_basic);
    RUN_TEST(test_file_apply_delta_insert);
    RUN_TEST(test_file_apply_delta_delete);
    RUN_TEST(test_file_apply_delta_replace);
    RUN_TEST(test_execute_file_delta_tool_call);
    
    return UNITY_END();
}