#ifndef FILE_TOOLS_H
#define FILE_TOOLS_H

#include "tools_system.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

/**
 * Maximum sizes for file tool operations
 */
#define FILE_MAX_PATH_LENGTH 4096
#define FILE_MAX_CONTENT_SIZE (1024 * 1024)  // 1MB max file size
#define FILE_MAX_SEARCH_RESULTS 1000
#define FILE_MAX_LIST_ENTRIES 1000

/**
 * File operation result codes
 */
typedef enum {
    FILE_SUCCESS = 0,
    FILE_ERROR_NOT_FOUND = -1,
    FILE_ERROR_PERMISSION = -2,
    FILE_ERROR_TOO_LARGE = -3,
    FILE_ERROR_INVALID_PATH = -4,
    FILE_ERROR_MEMORY = -5,
    FILE_ERROR_IO = -6
} FileErrorCode;

/**
 * Structure representing file information
 */
typedef struct {
    char *path;
    off_t size;
    mode_t permissions;
    time_t modified_time;
    time_t created_time;
    int is_directory;
    int is_executable;
    int is_readable;
    int is_writable;
} FileInfo;

/**
 * Structure representing a directory entry
 */
typedef struct {
    char *name;
    char *full_path;
    int is_directory;
    off_t size;
    time_t modified_time;
} DirectoryEntry;

/**
 * Structure for directory listing results
 */
typedef struct {
    DirectoryEntry *entries;
    int count;
    int total_files;
    int total_directories;
} DirectoryListing;

/**
 * Structure for search results
 */
typedef struct {
    char *file_path;
    int line_number;
    char *line_content;
    char *match_context;
} SearchResult;

/**
 * Structure for search operation results
 */
typedef struct {
    SearchResult *results;
    int count;
    int total_matches;
    int files_searched;
} SearchResults;

/**
 * Register all file manipulation tools with the tool registry
 * 
 * @param registry Pointer to ToolRegistry structure
 * @return 0 on success, -1 on failure
 */
int register_file_tools(ToolRegistry *registry);

/**
 * Read file contents with optional line range
 * 
 * @param file_path Path to file to read
 * @param start_line Starting line number (1-based, 0 for entire file)
 * @param end_line Ending line number (1-based, 0 for to end of file)
 * @param content Output buffer for file contents (caller must free)
 * @return FileErrorCode indicating success or failure
 */
FileErrorCode file_read_content(const char *file_path, int start_line, int end_line, char **content);

/**
 * Write content to file (overwrites existing file)
 * 
 * @param file_path Path to file to write
 * @param content Content to write
 * @param create_backup Whether to create backup if file exists
 * @return FileErrorCode indicating success or failure
 */
FileErrorCode file_write_content(const char *file_path, const char *content, int create_backup);

/**
 * Append content to file
 * 
 * @param file_path Path to file to append to
 * @param content Content to append
 * @return FileErrorCode indicating success or failure
 */
FileErrorCode file_append_content(const char *file_path, const char *content);

/**
 * List directory contents with optional filtering
 * 
 * @param directory_path Path to directory to list
 * @param pattern Optional pattern to filter results (NULL for all)
 * @param include_hidden Whether to include hidden files
 * @param recursive Whether to search recursively
 * @param listing Output directory listing (caller must free)
 * @return FileErrorCode indicating success or failure
 */
FileErrorCode file_list_directory(const char *directory_path, const char *pattern, 
                                 int include_hidden, int recursive, DirectoryListing *listing);

/**
 * Search for pattern in files
 * 
 * @param search_path Path to search (file or directory)
 * @param pattern Pattern to search for (regex supported)
 * @param file_pattern Optional file pattern filter (NULL for all files)
 * @param recursive Whether to search recursively in directories
 * @param case_sensitive Whether search is case sensitive
 * @param results Output search results (caller must free)
 * @return FileErrorCode indicating success or failure
 */
FileErrorCode file_search_content(const char *search_path, const char *pattern,
                                 const char *file_pattern, int recursive, 
                                 int case_sensitive, SearchResults *results);

/**
 * Get file information and metadata
 * 
 * @param file_path Path to file
 * @param info Output file information (caller must free)
 * @return FileErrorCode indicating success or failure
 */
FileErrorCode file_get_info(const char *file_path, FileInfo *info);

/**
 * Create backup copy of file
 * 
 * @param file_path Path to file to backup
 * @param backup_path Output path of created backup (caller must free)
 * @return FileErrorCode indicating success or failure
 */
FileErrorCode file_create_backup(const char *file_path, char **backup_path);

/**
 * Validate file path for security (prevent directory traversal, etc.)
 * 
 * @param file_path Path to validate
 * @return 1 if path is safe, 0 if potentially dangerous
 */
int file_validate_path(const char *file_path);

/**
 * Convert FileErrorCode to human-readable error message
 * 
 * @param error_code Error code to convert
 * @return Static error message string
 */
const char* file_error_message(FileErrorCode error_code);

/**
 * Clean up file information structure
 * 
 * @param info FileInfo structure to cleanup
 */
void cleanup_file_info(FileInfo *info);

/**
 * Clean up directory listing structure
 * 
 * @param listing DirectoryListing structure to cleanup
 */
void cleanup_directory_listing(DirectoryListing *listing);

/**
 * Clean up search results structure
 * 
 * @param results SearchResults structure to cleanup
 */
void cleanup_search_results(SearchResults *results);

/**
 * Tool call handlers for each file operation
 */
int execute_file_read_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_file_write_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_file_append_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_file_list_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_file_search_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_file_info_tool_call(const ToolCall *tool_call, ToolResult *result);

#endif // FILE_TOOLS_H