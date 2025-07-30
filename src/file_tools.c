#define _GNU_SOURCE  // For strcasestr
#include "file_tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <regex.h>
#include <libgen.h>
#include <sys/stat.h>
#include <time.h>
#include <strings.h>  // For strcasecmp
#include <limits.h>   // For PATH_MAX

// Helper function to safely duplicate strings
static char* safe_strdup(const char *str) {
    if (str == NULL) return NULL;
    char *result = malloc(strlen(str) + 1);
    if (result != NULL) {
        strcpy(result, str);
    }
    return result;
}

// Helper function to check if path is safe (prevent directory traversal)
int file_validate_path(const char *file_path) {
    if (file_path == NULL || strlen(file_path) == 0) {
        return 0;
    }
    
    if (strlen(file_path) >= FILE_MAX_PATH_LENGTH) {
        return 0;
    }
    
    // Check for directory traversal attempts
    if (strstr(file_path, "..") != NULL) {
        return 0;
    }
    
    // Check for null bytes
    if (strlen(file_path) != strcspn(file_path, "\0")) {
        return 0;
    }
    
    return 1;
}

// Convert error codes to messages
const char* file_error_message(FileErrorCode error_code) {
    switch (error_code) {
        case FILE_SUCCESS: return "Success";
        case FILE_ERROR_NOT_FOUND: return "File or directory not found";
        case FILE_ERROR_PERMISSION: return "Permission denied";
        case FILE_ERROR_TOO_LARGE: return "File too large";
        case FILE_ERROR_INVALID_PATH: return "Invalid file path";
        case FILE_ERROR_MEMORY: return "Memory allocation failed";
        case FILE_ERROR_IO: return "I/O error";
        default: return "Unknown error";
    }
}

// Read file content with optional line range
FileErrorCode file_read_content(const char *file_path, int start_line, int end_line, char **content) {
    if (file_path == NULL || content == NULL) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    if (!file_validate_path(file_path)) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    *content = NULL;
    
    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        return (errno == ENOENT) ? FILE_ERROR_NOT_FOUND : FILE_ERROR_PERMISSION;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size > FILE_MAX_CONTENT_SIZE) {
        fclose(file);
        return FILE_ERROR_TOO_LARGE;
    }
    
    if (start_line <= 0 && end_line <= 0) {
        // Read entire file
        *content = malloc(file_size + 1);
        if (*content == NULL) {
            fclose(file);
            return FILE_ERROR_MEMORY;
        }
        
        size_t bytes_read = fread(*content, 1, file_size, file);
        (*content)[bytes_read] = '\0';
        fclose(file);
        return FILE_SUCCESS;
    }
    
    // Read specific line range
    char *line = NULL;
    size_t line_len = 0;
    int current_line = 1;
    size_t total_content_size = 0;
    char *temp_content = malloc(FILE_MAX_CONTENT_SIZE);
    
    if (temp_content == NULL) {
        fclose(file);
        return FILE_ERROR_MEMORY;
    }
    
    temp_content[0] = '\0';
    
    while (getline(&line, &line_len, file) != -1) {
        if (current_line >= start_line && (end_line <= 0 || current_line <= end_line)) {
            size_t line_length = strlen(line);
            if (total_content_size + line_length >= FILE_MAX_CONTENT_SIZE) {
                break;
            }
            strcat(temp_content, line);
            total_content_size += line_length;
        }
        current_line++;
        if (end_line > 0 && current_line > end_line) {
            break;
        }
    }
    
    free(line);
    fclose(file);
    
    char *final_content = realloc(temp_content, total_content_size + 1);
    if (final_content == NULL) {
        free(temp_content);
        return FILE_ERROR_MEMORY;
    }
    *content = final_content;
    
    return FILE_SUCCESS;
}

// Write content to file
FileErrorCode file_write_content(const char *file_path, const char *content, int create_backup) {
    if (file_path == NULL || content == NULL) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    if (!file_validate_path(file_path)) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    // Create backup if requested and file exists
    if (create_backup && access(file_path, F_OK) == 0) {
        char *backup_path = NULL;
        FileErrorCode backup_result = file_create_backup(file_path, &backup_path);
        if (backup_result != FILE_SUCCESS) {
            return backup_result;
        }
        free(backup_path);
    }
    
    FILE *file = fopen(file_path, "w");
    if (file == NULL) {
        return (errno == EACCES) ? FILE_ERROR_PERMISSION : FILE_ERROR_IO;
    }
    
    size_t content_len = strlen(content);
    size_t written = fwrite(content, 1, content_len, file);
    fclose(file);
    
    if (written != content_len) {
        return FILE_ERROR_IO;
    }
    
    return FILE_SUCCESS;
}

// Append content to file
FileErrorCode file_append_content(const char *file_path, const char *content) {
    if (file_path == NULL || content == NULL) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    if (!file_validate_path(file_path)) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    FILE *file = fopen(file_path, "a");
    if (file == NULL) {
        return (errno == EACCES) ? FILE_ERROR_PERMISSION : FILE_ERROR_IO;
    }
    
    size_t content_len = strlen(content);
    size_t written = fwrite(content, 1, content_len, file);
    fclose(file);
    
    if (written != content_len) {
        return FILE_ERROR_IO;
    }
    
    return FILE_SUCCESS;
}

// Get file information
FileErrorCode file_get_info(const char *file_path, FileInfo *info) {
    if (file_path == NULL || info == NULL) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    if (!file_validate_path(file_path)) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    memset(info, 0, sizeof(FileInfo));
    
    struct stat file_stat;
    if (stat(file_path, &file_stat) != 0) {
        return (errno == ENOENT) ? FILE_ERROR_NOT_FOUND : FILE_ERROR_PERMISSION;
    }
    
    info->path = safe_strdup(file_path);
    info->size = file_stat.st_size;
    info->permissions = file_stat.st_mode;
    info->modified_time = file_stat.st_mtime;
    info->created_time = file_stat.st_ctime;
    info->is_directory = S_ISDIR(file_stat.st_mode);
    info->is_executable = (file_stat.st_mode & S_IXUSR) != 0;
    info->is_readable = (file_stat.st_mode & S_IRUSR) != 0;
    info->is_writable = (file_stat.st_mode & S_IWUSR) != 0;
    
    if (info->path == NULL) {
        return FILE_ERROR_MEMORY;
    }
    
    return FILE_SUCCESS;
}

// Create backup of file
FileErrorCode file_create_backup(const char *file_path, char **backup_path) {
    if (file_path == NULL || backup_path == NULL) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    if (!file_validate_path(file_path)) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    *backup_path = NULL;
    
    // Generate backup filename with timestamp
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);
    
    size_t backup_path_len = strlen(file_path) + strlen(timestamp) + 10;
    *backup_path = malloc(backup_path_len);
    if (*backup_path == NULL) {
        return FILE_ERROR_MEMORY;
    }
    
    snprintf(*backup_path, backup_path_len, "%s.backup_%s", file_path, timestamp);
    
    // Copy file content
    char *content = NULL;
    FileErrorCode read_result = file_read_content(file_path, 0, 0, &content);
    if (read_result != FILE_SUCCESS) {
        free(*backup_path);
        *backup_path = NULL;
        return read_result;
    }
    
    FileErrorCode write_result = file_write_content(*backup_path, content, 0);
    free(content);
    
    if (write_result != FILE_SUCCESS) {
        free(*backup_path);
        *backup_path = NULL;
        return write_result;
    }
    
    return FILE_SUCCESS;
}

// List directory contents
FileErrorCode file_list_directory(const char *directory_path, const char *pattern, 
                                 int include_hidden, int recursive, DirectoryListing *listing) {
    (void)recursive; // TODO: Implement recursive directory traversal
    if (directory_path == NULL || listing == NULL) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    if (!file_validate_path(directory_path)) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    memset(listing, 0, sizeof(DirectoryListing));
    
    DIR *dir = opendir(directory_path);
    if (dir == NULL) {
        return (errno == ENOENT) ? FILE_ERROR_NOT_FOUND : FILE_ERROR_PERMISSION;
    }
    
    // Allocate initial space for entries
    int capacity = 100;
    listing->entries = malloc(capacity * sizeof(DirectoryEntry));
    if (listing->entries == NULL) {
        closedir(dir);
        return FILE_ERROR_MEMORY;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && listing->count < FILE_MAX_LIST_ENTRIES) {
        // Skip . and .. entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Skip hidden files if not requested
        if (!include_hidden && entry->d_name[0] == '.') {
            continue;
        }
        
        // Apply pattern filter if provided
        if (pattern != NULL && strstr(entry->d_name, pattern) == NULL) {
            continue;
        }
        
        // Expand capacity if needed
        if (listing->count >= capacity) {
            capacity *= 2;
            DirectoryEntry *new_entries = realloc(listing->entries, capacity * sizeof(DirectoryEntry));
            if (new_entries == NULL) {
                cleanup_directory_listing(listing);
                closedir(dir);
                return FILE_ERROR_MEMORY;
            }
            listing->entries = new_entries;
        }
        
        DirectoryEntry *dir_entry = &listing->entries[listing->count];
        memset(dir_entry, 0, sizeof(DirectoryEntry));
        
        dir_entry->name = safe_strdup(entry->d_name);
        
        // Build full path
        size_t full_path_len = strlen(directory_path) + strlen(entry->d_name) + 2;
        dir_entry->full_path = malloc(full_path_len);
        if (dir_entry->full_path != NULL) {
            snprintf(dir_entry->full_path, full_path_len, "%s/%s", directory_path, entry->d_name);
        }
        
        // Get file info
        struct stat file_stat;
        if (dir_entry->full_path != NULL && stat(dir_entry->full_path, &file_stat) == 0) {
            dir_entry->is_directory = S_ISDIR(file_stat.st_mode);
            dir_entry->size = file_stat.st_size;
            dir_entry->modified_time = file_stat.st_mtime;
            
            if (dir_entry->is_directory) {
                listing->total_directories++;
            } else {
                listing->total_files++;
            }
        }
        
        if (dir_entry->name == NULL || dir_entry->full_path == NULL) {
            free(dir_entry->name);
            free(dir_entry->full_path);
            cleanup_directory_listing(listing);
            closedir(dir);
            return FILE_ERROR_MEMORY;
        }
        
        listing->count++;
    }
    
    closedir(dir);
    return FILE_SUCCESS;
}

// Helper function to search content in a single file
static FileErrorCode search_file_content(const char *file_path, const char *pattern,
                                        int case_sensitive, SearchResults *results, int *capacity) {
    char *content = NULL;
    FileErrorCode read_result = file_read_content(file_path, 0, 0, &content);
    if (read_result != FILE_SUCCESS) {
        return read_result;
    }
    
    // Simple line-by-line search
    char *line_start = content;
    char *line_end;
    int line_number = 1;
    
    while (*line_start != '\0' && results->count < FILE_MAX_SEARCH_RESULTS) {
        line_end = strchr(line_start, '\n');
        if (line_end == NULL) {
            line_end = line_start + strlen(line_start);
        }
        
        // Create null-terminated line
        size_t line_len = line_end - line_start;
        char *line = malloc(line_len + 1);
        if (line == NULL) {
            free(content);
            return FILE_ERROR_MEMORY;
        }
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';
        
        // Search for pattern in line
        char *match = case_sensitive ? strstr(line, pattern) : strcasestr(line, pattern);
        if (match != NULL) {
            // Expand capacity if needed
            if (results->count >= *capacity) {
                *capacity *= 2;
                SearchResult *new_results = realloc(results->results, *capacity * sizeof(SearchResult));
                if (new_results == NULL) {
                    free(line);
                    free(content);
                    return FILE_ERROR_MEMORY;
                }
                results->results = new_results;
            }
            
            SearchResult *result = &results->results[results->count];
            memset(result, 0, sizeof(SearchResult));
            
            result->file_path = safe_strdup(file_path);
            result->line_number = line_number;
            result->line_content = safe_strdup(line);
            result->match_context = safe_strdup(line); // Simple context for now
            
            if (result->file_path == NULL || result->line_content == NULL || result->match_context == NULL) {
                free(result->file_path);
                free(result->line_content);
                free(result->match_context);
                free(line);
                free(content);
                return FILE_ERROR_MEMORY;
            }
            
            results->count++;
            results->total_matches++;
        }
        
        free(line);
        line_number++;
        
        if (*line_end == '\n') {
            line_start = line_end + 1;
        } else {
            break;
        }
    }
    
    free(content);
    return FILE_SUCCESS;
}

// Helper function to search content within a directory
static FileErrorCode search_directory_content(const char *dir_path, const char *pattern,
                                            int recursive, int case_sensitive,
                                            SearchResults *results, int *capacity) {
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        return (errno == EACCES) ? FILE_ERROR_PERMISSION : FILE_ERROR_NOT_FOUND;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Build full path
        char full_path[PATH_MAX];
        int path_len = snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        if (path_len >= (int)sizeof(full_path)) {
            // Path too long, skip
            continue;
        }
        
        struct stat entry_stat;
        if (stat(full_path, &entry_stat) != 0) {
            continue; // Skip if we can't stat the file
        }
        
        if (S_ISREG(entry_stat.st_mode)) {
            // Regular file - search its content
            FileErrorCode search_result = search_file_content(full_path, pattern, case_sensitive, results, capacity);
            if (search_result != FILE_SUCCESS && search_result != FILE_ERROR_NOT_FOUND) {
                closedir(dir);
                return search_result;
            }
            results->files_searched++;
        } else if (S_ISDIR(entry_stat.st_mode) && recursive) {
            // Directory - recurse if requested
            FileErrorCode search_result = search_directory_content(full_path, pattern, recursive, case_sensitive, results, capacity);
            if (search_result != FILE_SUCCESS) {
                closedir(dir);
                return search_result;
            }
        }
    }
    
    closedir(dir);
    return FILE_SUCCESS;
}

// Search for pattern in files (basic implementation)
FileErrorCode file_search_content(const char *search_path, const char *pattern,
                                 const char *file_pattern, int recursive, 
                                 int case_sensitive, SearchResults *results) {
    (void)file_pattern; // TODO: Implement file pattern filtering
    (void)recursive; // TODO: Implement recursive search
    if (search_path == NULL || pattern == NULL || results == NULL) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    if (!file_validate_path(search_path)) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    memset(results, 0, sizeof(SearchResults));
    
    // For now, implement simple string search in a single file
    // Full implementation would handle directories, regex, etc.
    
    struct stat path_stat;
    if (stat(search_path, &path_stat) != 0) {
        return (errno == ENOENT) ? FILE_ERROR_NOT_FOUND : FILE_ERROR_PERMISSION;
    }
    
    if (S_ISDIR(path_stat.st_mode)) {
        // Search within directory
        // Initialize results array
        int capacity = 50;
        results->results = malloc(capacity * sizeof(SearchResult));
        if (results->results == NULL) {
            return FILE_ERROR_MEMORY;
        }
        return search_directory_content(search_path, pattern, recursive, case_sensitive, results, &capacity);
    }
    
    // Search in single file
    char *content = NULL;
    FileErrorCode read_result = file_read_content(search_path, 0, 0, &content);
    if (read_result != FILE_SUCCESS) {
        return read_result;
    }
    
    // Allocate results array
    int capacity = 50;
    results->results = malloc(capacity * sizeof(SearchResult));
    if (results->results == NULL) {
        free(content);
        return FILE_ERROR_MEMORY;
    }
    
    // Simple line-by-line search
    char *line_start = content;
    char *line_end;
    int line_number = 1;
    
    while (*line_start != '\0' && results->count < FILE_MAX_SEARCH_RESULTS) {
        line_end = strchr(line_start, '\n');
        if (line_end == NULL) {
            line_end = line_start + strlen(line_start);
        }
        
        // Create null-terminated line
        size_t line_len = line_end - line_start;
        char *line = malloc(line_len + 1);
        if (line == NULL) {
            cleanup_search_results(results);
            free(content);
            return FILE_ERROR_MEMORY;
        }
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';
        
        // Search for pattern in line
        char *match = case_sensitive ? strstr(line, pattern) : strcasestr(line, pattern);
        if (match != NULL) {
            // Expand capacity if needed
            if (results->count >= capacity) {
                capacity *= 2;
                SearchResult *new_results = realloc(results->results, capacity * sizeof(SearchResult));
                if (new_results == NULL) {
                    free(line);
                    cleanup_search_results(results);
                    free(content);
                    return FILE_ERROR_MEMORY;
                }
                results->results = new_results;
            }
            
            SearchResult *result = &results->results[results->count];
            memset(result, 0, sizeof(SearchResult));
            
            result->file_path = safe_strdup(search_path);
            result->line_number = line_number;
            result->line_content = safe_strdup(line);
            result->match_context = safe_strdup(line); // Simple context for now
            
            if (result->file_path == NULL || result->line_content == NULL || result->match_context == NULL) {
                free(result->file_path);
                free(result->line_content);
                free(result->match_context);
                free(line);
                cleanup_search_results(results);
                free(content);
                return FILE_ERROR_MEMORY;
            }
            
            results->count++;
            results->total_matches++;
        }
        
        free(line);
        line_number++;
        
        if (*line_end == '\n') {
            line_start = line_end + 1;
        } else {
            break;
        }
    }
    
    results->files_searched = 1;
    free(content);
    
    return FILE_SUCCESS;
}

// Cleanup functions
void cleanup_file_info(FileInfo *info) {
    if (info == NULL) return;
    
    free(info->path);
    memset(info, 0, sizeof(FileInfo));
}

void cleanup_directory_listing(DirectoryListing *listing) {
    if (listing == NULL) return;
    
    for (int i = 0; i < listing->count; i++) {
        free(listing->entries[i].name);
        free(listing->entries[i].full_path);
    }
    
    free(listing->entries);
    memset(listing, 0, sizeof(DirectoryListing));
}

void cleanup_search_results(SearchResults *results) {
    if (results == NULL) return;
    
    for (int i = 0; i < results->count; i++) {
        free(results->results[i].file_path);
        free(results->results[i].line_content);
        free(results->results[i].match_context);
    }
    
    free(results->results);
    memset(results, 0, sizeof(SearchResults));
}

// JSON parsing helpers for tool calls
// Helper function to unescape JSON string values
static void unescape_json_string(char *str) {
    if (!str) return;
    
    char *src = str;
    char *dst = str;
    
    while (*src) {
        if (*src == '\\' && *(src + 1)) {
            src++; // Skip backslash
            switch (*src) {
                case '"': *dst++ = '"'; break;
                case '\\': *dst++ = '\\'; break;
                case 'n': *dst++ = '\n'; break;
                case 'r': *dst++ = '\r'; break;
                case 't': *dst++ = '\t'; break;
                case 'b': *dst++ = '\b'; break;
                case 'f': *dst++ = '\f'; break;
                default: *dst++ = *src; break;
            }
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static char* extract_string_param(const char *json, const char *param_name) {
    char search_key[256] = {0};
    snprintf(search_key, sizeof(search_key), "\"%s\":", param_name);
    
    const char *start = strstr(json, search_key);
    if (start == NULL) {
        return NULL;
    }
    
    start += strlen(search_key);
    while (*start == ' ' || *start == '\t') start++;
    
    if (*start != '"') return NULL;
    start++; // Skip opening quote
    
    const char *end = start;
    while (*end != '\0' && *end != '"') {
        if (*end == '\\' && *(end + 1) != '\0') {
            end += 2; // Skip escaped character
        } else {
            end++;
        }
    }
    
    if (*end != '"') return NULL;
    
    size_t len = end - start;
    char *result = malloc(len + 1);
    if (result == NULL) return NULL;
    
    memcpy(result, start, len);
    result[len] = '\0';
    
    return result;
}

static int extract_int_param(const char *json, const char *param_name, int default_value) {
    char search_key[256] = {0};
    snprintf(search_key, sizeof(search_key), "\"%s\":", param_name);
    
    const char *start = strstr(json, search_key);
    if (start == NULL) return default_value;
    
    start += strlen(search_key);
    while (*start == ' ' || *start == '\t') start++;
    
    return atoi(start);
}

static int extract_bool_param(const char *json, const char *param_name, int default_value) {
    char search_key[256] = {0};
    snprintf(search_key, sizeof(search_key), "\"%s\":", param_name);
    
    const char *start = strstr(json, search_key);
    if (start == NULL) return default_value;
    
    start += strlen(search_key);
    while (*start == ' ' || *start == '\t') start++;
    
    if (strncmp(start, "true", 4) == 0) return 1;
    if (strncmp(start, "false", 5) == 0) return 0;
    
    return default_value;
}

// Implement missing tool call handlers
int execute_file_write_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    // Unescape the entire arguments JSON string first
    char *unescaped_args = NULL;
    if (tool_call->arguments != NULL) {
        unescaped_args = safe_strdup(tool_call->arguments);
        if (unescaped_args != NULL) {
            unescape_json_string(unescaped_args);
        }
    }
    
    char *file_path = extract_string_param(unescaped_args, "file_path");
    char *content = extract_string_param(unescaped_args, "content");
    int create_backup = extract_bool_param(unescaped_args, "create_backup", 0);
    
    free(unescaped_args);
    
    if (file_path == NULL || content == NULL) {
        result->result = safe_strdup("Error: Missing required parameters 'file_path' or 'content'");
        result->success = 0;
        free(file_path);
        free(content);
        return 0;
    }
    
    FileErrorCode error = file_write_content(file_path, content, create_backup);
    
    char result_msg[512];
    if (error == FILE_SUCCESS) {
        snprintf(result_msg, sizeof(result_msg),
            "{\"success\": true, \"file_path\": \"%s\", \"bytes_written\": %zu, \"backup_created\": %s}",
            file_path, strlen(content), create_backup ? "true" : "false");
        result->success = 1;
    } else {
        snprintf(result_msg, sizeof(result_msg),
            "{\"success\": false, \"error\": \"%s\", \"file_path\": \"%s\"}",
            file_error_message(error), file_path);
        result->success = 0;
    }
    
    result->result = safe_strdup(result_msg);
    
    free(file_path);
    free(content);
    return 0;
}

int execute_file_append_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    char *file_path = extract_string_param(tool_call->arguments, "file_path");
    char *content = extract_string_param(tool_call->arguments, "content");
    
    if (file_path == NULL || content == NULL) {
        result->result = safe_strdup("Error: Missing required parameters 'file_path' or 'content'");
        result->success = 0;
        free(file_path);
        free(content);
        return 0;
    }
    
    FileErrorCode error = file_append_content(file_path, content);
    
    char result_msg[512];
    if (error == FILE_SUCCESS) {
        snprintf(result_msg, sizeof(result_msg),
            "{\"success\": true, \"file_path\": \"%s\", \"bytes_appended\": %zu}",
            file_path, strlen(content));
        result->success = 1;
    } else {
        snprintf(result_msg, sizeof(result_msg),
            "{\"success\": false, \"error\": \"%s\", \"file_path\": \"%s\"}",
            file_error_message(error), file_path);
        result->success = 0;
    }
    
    result->result = safe_strdup(result_msg);
    
    free(file_path);
    free(content);
    return 0;
}

int execute_file_list_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    char *directory_path = extract_string_param(tool_call->arguments, "directory_path");
    if (directory_path == NULL) {
        result->result = safe_strdup("Error: Missing required parameter 'directory_path'");
        result->success = 0;
        return 0;
    }
    
    char *pattern = extract_string_param(tool_call->arguments, "pattern");
    int include_hidden = extract_bool_param(tool_call->arguments, "include_hidden", 0);
    
    DirectoryListing listing;
    FileErrorCode error = file_list_directory(directory_path, pattern, include_hidden, 0, &listing);
    
    if (error == FILE_SUCCESS) {
        // Build JSON response
        size_t result_size = listing.count * 200 + 1000;
        char *json_result = malloc(result_size);
        if (json_result != NULL) {
            strcpy(json_result, "{\"success\": true, \"entries\": [");
            
            for (int i = 0; i < listing.count; i++) {
                if (i > 0) strcat(json_result, ", ");
                
                char entry_json[300];
                snprintf(entry_json, sizeof(entry_json),
                    "{\"name\": \"%s\", \"full_path\": \"%s\", \"is_directory\": %s, \"size\": %ld}",
                    listing.entries[i].name,
                    listing.entries[i].full_path,
                    listing.entries[i].is_directory ? "true" : "false",
                    listing.entries[i].size);
                strcat(json_result, entry_json);
            }
            
            char summary[200];
            snprintf(summary, sizeof(summary), 
                "], \"total_files\": %d, \"total_directories\": %d, \"total_entries\": %d}",
                listing.total_files, listing.total_directories, listing.count);
            strcat(json_result, summary);
            
            result->result = json_result;
            result->success = 1;
        } else {
            result->result = safe_strdup("Error: Memory allocation failed");
            result->success = 0;
        }
        
        cleanup_directory_listing(&listing);
    } else {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
            "{\"success\": false, \"error\": \"%s\", \"directory_path\": \"%s\"}",
            file_error_message(error), directory_path);
        result->result = safe_strdup(error_msg);
        result->success = 0;
    }
    
    free(directory_path);
    free(pattern);
    return 0;
}

int execute_file_search_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    char *search_path = extract_string_param(tool_call->arguments, "search_path");
    char *pattern = extract_string_param(tool_call->arguments, "pattern");
    
    if (search_path == NULL || pattern == NULL) {
        result->result = safe_strdup("Error: Missing required parameters 'search_path' or 'pattern'");
        result->success = 0;
        free(search_path);
        free(pattern);
        return 0;
    }
    
    int case_sensitive = extract_bool_param(tool_call->arguments, "case_sensitive", 1);
    
    SearchResults search_results;
    FileErrorCode error = file_search_content(search_path, pattern, NULL, 0, case_sensitive, &search_results);
    
    if (error == FILE_SUCCESS) {
        size_t result_size = search_results.count * 300 + 1000;
        char *json_result = malloc(result_size);
        if (json_result != NULL) {
            strcpy(json_result, "{\"success\": true, \"matches\": [");
            
            for (int i = 0; i < search_results.count; i++) {
                if (i > 0) strcat(json_result, ", ");
                
                char match_json[400];
                snprintf(match_json, sizeof(match_json),
                    "{\"file\": \"%s\", \"line\": %d, \"content\": \"%s\"}",
                    search_results.results[i].file_path,
                    search_results.results[i].line_number,
                    search_results.results[i].line_content);
                strcat(json_result, match_json);
            }
            
            char summary[200];
            snprintf(summary, sizeof(summary),
                "], \"total_matches\": %d, \"files_searched\": %d}",
                search_results.total_matches, search_results.files_searched);
            strcat(json_result, summary);
            
            result->result = json_result;
            result->success = 1;
        } else {
            result->result = safe_strdup("Error: Memory allocation failed");
            result->success = 0;
        }
        
        cleanup_search_results(&search_results);
    } else {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
            "{\"success\": false, \"error\": \"%s\", \"search_path\": \"%s\"}",
            file_error_message(error), search_path);
        result->result = safe_strdup(error_msg);
        result->success = 0;
    }
    
    free(search_path);
    free(pattern);
    return 0;
}

int execute_file_info_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    char *file_path = extract_string_param(tool_call->arguments, "file_path");
    if (file_path == NULL) {
        result->result = safe_strdup("Error: Missing required parameter 'file_path'");
        result->success = 0;
        return 0;
    }
    
    FileInfo info;
    FileErrorCode error = file_get_info(file_path, &info);
    
    if (error == FILE_SUCCESS) {
        char result_msg[1000];
        snprintf(result_msg, sizeof(result_msg),
            "{\"success\": true, \"path\": \"%s\", \"size\": %ld, "
            "\"is_directory\": %s, \"is_executable\": %s, \"is_readable\": %s, \"is_writable\": %s, "
            "\"modified_time\": %ld, \"permissions\": %o}",
            info.path, info.size,
            info.is_directory ? "true" : "false",
            info.is_executable ? "true" : "false", 
            info.is_readable ? "true" : "false",
            info.is_writable ? "true" : "false",
            info.modified_time, info.permissions);
        
        result->result = safe_strdup(result_msg);
        result->success = 1;
        
        cleanup_file_info(&info);
    } else {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
            "{\"success\": false, \"error\": \"%s\", \"file_path\": \"%s\"}",
            file_error_message(error), file_path);
        result->result = safe_strdup(error_msg);
        result->success = 0;
    }
    
    free(file_path);
    return 0;
}

// Tool call handlers
int execute_file_read_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    char *file_path = extract_string_param(tool_call->arguments, "file_path");
    if (file_path == NULL) {
        result->result = safe_strdup("Error: Missing required parameter 'file_path'");
        result->success = 0;
        return 0;
    }
    
    int start_line = extract_int_param(tool_call->arguments, "start_line", 0);
    int end_line = extract_int_param(tool_call->arguments, "end_line", 0);
    
    char *content = NULL;
    FileErrorCode error = file_read_content(file_path, start_line, end_line, &content);
    
    if (error == FILE_SUCCESS && content != NULL) {
        // Format as JSON result
        size_t result_size = strlen(content) * 2 + 1000; // *2 for escaping
        char *json_result = malloc(result_size);
        if (json_result != NULL) {
            snprintf(json_result, result_size, 
                "{\"success\": true, \"file_path\": \"%s\", \"content\": \"%s\", \"lines_read\": %d}",
                file_path, content, (end_line > start_line) ? (end_line - start_line + 1) : -1);
            result->result = json_result;
        } else {
            result->result = safe_strdup("Error: Memory allocation failed");
        }
        result->success = (json_result != NULL) ? 1 : 0;
        free(content);
    } else {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
            "{\"success\": false, \"error\": \"%s\", \"file_path\": \"%s\"}", 
            file_error_message(error), file_path);
        result->result = safe_strdup(error_msg);
        result->success = 0;
    }
    
    free(file_path);
    return 0;
}

// Helper function to register a single tool
static int register_single_tool(ToolRegistry *registry, const char *name, const char *description, 
                                ToolParameter *params, int param_count) {
    ToolFunction *new_functions = realloc(registry->functions, 
                                         (registry->function_count + 1) * sizeof(ToolFunction));
    if (new_functions == NULL) return -1;
    
    registry->functions = new_functions;
    ToolFunction *func = &registry->functions[registry->function_count];
    
    func->name = safe_strdup(name);
    func->description = safe_strdup(description);
    func->parameter_count = param_count;
    func->parameters = params;
    
    if (func->name == NULL || func->description == NULL) {
        free(func->name);
        free(func->description);
        return -1;
    }
    
    registry->function_count++;
    return 0;
}

// Register all file tools
int register_file_tools(ToolRegistry *registry) {
    if (registry == NULL) return -1;
    
    // 1. Register file_read tool
    ToolParameter *read_params = malloc(3 * sizeof(ToolParameter));
    if (read_params == NULL) return -1;
    
    read_params[0] = (ToolParameter){"file_path", "string", "Path to the file to read", NULL, 0, 1};
    read_params[1] = (ToolParameter){"start_line", "number", "Starting line number (1-based, 0 for entire file)", NULL, 0, 0};
    read_params[2] = (ToolParameter){"end_line", "number", "Ending line number (1-based, 0 for to end of file)", NULL, 0, 0};
    
    for (int i = 0; i < 3; i++) {
        read_params[i].name = safe_strdup(read_params[i].name);
        read_params[i].type = safe_strdup(read_params[i].type);
        read_params[i].description = safe_strdup(read_params[i].description);
        if (!read_params[i].name || !read_params[i].type || !read_params[i].description) {
            for (int j = 0; j <= i; j++) {
                free(read_params[j].name);
                free(read_params[j].type);
                free(read_params[j].description);
            }
            free(read_params);
            return -1;
        }
    }
    
    if (register_single_tool(registry, "file_read", "Read file contents with optional line range", read_params, 3) != 0) {
        return -1;
    }
    
    // 2. Register file_write tool  
    ToolParameter *write_params = malloc(3 * sizeof(ToolParameter));
    if (write_params == NULL) return -1;
    
    write_params[0] = (ToolParameter){"file_path", "string", "Path to the file to write", NULL, 0, 1};
    write_params[1] = (ToolParameter){"content", "string", "Content to write to file", NULL, 0, 1};
    write_params[2] = (ToolParameter){"create_backup", "boolean", "Create backup before overwriting (default: false)", NULL, 0, 0};
    
    for (int i = 0; i < 3; i++) {
        write_params[i].name = safe_strdup(write_params[i].name);
        write_params[i].type = safe_strdup(write_params[i].type);
        write_params[i].description = safe_strdup(write_params[i].description);
        if (!write_params[i].name || !write_params[i].type || !write_params[i].description) {
            for (int j = 0; j <= i; j++) {
                free(write_params[j].name);
                free(write_params[j].type);
                free(write_params[j].description);
            }
            free(write_params);
            return -1;
        }
    }
    
    if (register_single_tool(registry, "file_write", "Write content to file with optional backup", write_params, 3) != 0) {
        return -1;
    }
    
    // 3. Register file_append tool
    ToolParameter *append_params = malloc(2 * sizeof(ToolParameter));
    if (append_params == NULL) return -1;
    
    append_params[0] = (ToolParameter){"file_path", "string", "Path to the file to append to", NULL, 0, 1};
    append_params[1] = (ToolParameter){"content", "string", "Content to append to file", NULL, 0, 1};
    
    for (int i = 0; i < 2; i++) {
        append_params[i].name = safe_strdup(append_params[i].name);
        append_params[i].type = safe_strdup(append_params[i].type);
        append_params[i].description = safe_strdup(append_params[i].description);
        if (!append_params[i].name || !append_params[i].type || !append_params[i].description) {
            for (int j = 0; j <= i; j++) {
                free(append_params[j].name);
                free(append_params[j].type);
                free(append_params[j].description);
            }
            free(append_params);
            return -1;
        }
    }
    
    if (register_single_tool(registry, "file_append", "Append content to existing file", append_params, 2) != 0) {
        return -1;
    }
    
    // 4. Register file_list tool
    ToolParameter *list_params = malloc(3 * sizeof(ToolParameter));
    if (list_params == NULL) return -1;
    
    list_params[0] = (ToolParameter){"directory_path", "string", "Path to directory to list", NULL, 0, 1};
    list_params[1] = (ToolParameter){"pattern", "string", "Optional pattern to filter files", NULL, 0, 0};
    list_params[2] = (ToolParameter){"include_hidden", "boolean", "Include hidden files (default: false)", NULL, 0, 0};
    
    for (int i = 0; i < 3; i++) {
        list_params[i].name = safe_strdup(list_params[i].name);
        list_params[i].type = safe_strdup(list_params[i].type);
        list_params[i].description = safe_strdup(list_params[i].description);
        if (!list_params[i].name || !list_params[i].type || !list_params[i].description) {
            for (int j = 0; j <= i; j++) {
                free(list_params[j].name);
                free(list_params[j].type);
                free(list_params[j].description);
            }
            free(list_params);
            return -1;
        }
    }
    
    if (register_single_tool(registry, "file_list", "List directory contents with optional filtering", list_params, 3) != 0) {
        return -1;
    }
    
    // 5. Register file_search tool
    ToolParameter *search_params = malloc(3 * sizeof(ToolParameter));
    if (search_params == NULL) return -1;
    
    search_params[0] = (ToolParameter){"search_path", "string", "File or directory path to search", NULL, 0, 1};
    search_params[1] = (ToolParameter){"pattern", "string", "Text pattern to search for", NULL, 0, 1};
    search_params[2] = (ToolParameter){"case_sensitive", "boolean", "Case sensitive search (default: true)", NULL, 0, 0};
    
    for (int i = 0; i < 3; i++) {
        search_params[i].name = safe_strdup(search_params[i].name);
        search_params[i].type = safe_strdup(search_params[i].type);
        search_params[i].description = safe_strdup(search_params[i].description);
        if (!search_params[i].name || !search_params[i].type || !search_params[i].description) {
            for (int j = 0; j <= i; j++) {
                free(search_params[j].name);
                free(search_params[j].type);
                free(search_params[j].description);
            }
            free(search_params);
            return -1;
        }
    }
    
    if (register_single_tool(registry, "file_search", "Search for text patterns in files", search_params, 3) != 0) {
        return -1;
    }
    
    // 6. Register file_info tool
    ToolParameter *info_params = malloc(1 * sizeof(ToolParameter));
    if (info_params == NULL) return -1;
    
    info_params[0] = (ToolParameter){"file_path", "string", "Path to file to get information about", NULL, 0, 1};
    
    info_params[0].name = safe_strdup(info_params[0].name);
    info_params[0].type = safe_strdup(info_params[0].type);
    info_params[0].description = safe_strdup(info_params[0].description);
    if (!info_params[0].name || !info_params[0].type || !info_params[0].description) {
        free(info_params[0].name);
        free(info_params[0].type);
        free(info_params[0].description);
        free(info_params);
        return -1;
    }
    
    if (register_single_tool(registry, "file_info", "Get detailed file information and metadata", info_params, 1) != 0) {
        return -1;
    }
    
    return 0;
}