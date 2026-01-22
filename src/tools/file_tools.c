#define _GNU_SOURCE  // For strcasestr
#include "file_tools.h"
#include "json_escape.h"
#include "output_formatter.h"
#include <cJSON.h>
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
#include <math.h>     // For ceil

// Forward declarations
static FileErrorCode list_directory_recursive(const char *directory_path, const char *pattern,
                                             int include_hidden, DirectoryListing *listing, int *capacity);

// Maximum file size for searching (1MB - same as FILE_MAX_CONTENT_SIZE)
#define FILE_SEARCH_MAX_SIZE (1024 * 1024)

// Directories to skip during recursive search
static const char *SKIP_DIRECTORIES[] = {
    ".git",
    ".svn",
    ".hg",
    "node_modules",
    "__pycache__",
    ".cache",
    "build",
    "dist",
    "deps",
    "vendor",
    ".venv",
    "venv",
    ".tox",
    "target",       // Rust/Maven build output
    "out",          // Common output directory
    ".next",        // Next.js
    ".nuxt",        // Nuxt.js
    "coverage",     // Test coverage
    ".terraform",   // Terraform
    NULL
};

// Binary file extensions to skip
static const char *BINARY_EXTENSIONS[] = {
    // Executables and libraries
    ".exe", ".dll", ".so", ".dylib", ".a", ".o", ".obj", ".lib",
    ".com", ".bin", ".elf", ".dbg",
    // Archives
    ".zip", ".tar", ".gz", ".bz2", ".xz", ".7z", ".rar", ".tgz",
    ".jar", ".war", ".ear",
    // Images
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".ico", ".svg", ".webp",
    ".tiff", ".tif", ".psd", ".raw", ".heic",
    // Audio/Video
    ".mp3", ".mp4", ".avi", ".mov", ".mkv", ".flv", ".wmv", ".wav",
    ".ogg", ".m4a", ".aac", ".flac", ".wma",
    // Documents (binary)
    ".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx",
    ".odt", ".ods", ".odp",
    // Fonts
    ".ttf", ".otf", ".woff", ".woff2", ".eot",
    // Database
    ".db", ".sqlite", ".sqlite3", ".mdb",
    // Other binary formats
    ".pyc", ".pyo", ".class", ".wasm",
    ".ico", ".icns",
    NULL
};

// Helper function to check if a directory should be skipped
static int should_skip_directory(const char *dirname) {
    if (dirname == NULL) return 0;

    for (int i = 0; SKIP_DIRECTORIES[i] != NULL; i++) {
        if (strcmp(dirname, SKIP_DIRECTORIES[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

// Helper function to check if a file has a binary extension
static int has_binary_extension(const char *filename) {
    if (filename == NULL) return 0;

    const char *dot = strrchr(filename, '.');
    if (dot == NULL) return 0;

    for (int i = 0; BINARY_EXTENSIONS[i] != NULL; i++) {
        if (strcasecmp(dot, BINARY_EXTENSIONS[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

// Helper function to check if file content appears to be binary
// Checks the first portion of the file for null bytes
static int is_binary_content(const char *file_path) {
    if (file_path == NULL) return 0;

    FILE *file = fopen(file_path, "rb");
    if (file == NULL) return 0;

    // Read first 8KB to check for binary content
    unsigned char buffer[8192];
    size_t bytes_read = fread(buffer, 1, sizeof(buffer), file);
    fclose(file);

    // Check for null bytes (strong indicator of binary)
    for (size_t i = 0; i < bytes_read; i++) {
        if (buffer[i] == 0) {
            return 1;
        }
    }

    // Check for high ratio of non-printable characters
    int non_printable = 0;
    for (size_t i = 0; i < bytes_read; i++) {
        unsigned char c = buffer[i];
        // Allow common text characters: printable ASCII, newline, tab, carriage return
        if (c < 32 && c != '\n' && c != '\r' && c != '\t') {
            non_printable++;
        }
        // High bytes (>127) in non-UTF8 context suggest binary
        if (c > 127) {
            // Could be UTF-8, allow some tolerance
            // But if we see many, it's likely binary
            non_printable++;
        }
    }

    // If more than 30% non-printable, consider it binary
    if (bytes_read > 0 && (non_printable * 100 / bytes_read) > 30) {
        return 1;
    }

    return 0;
}

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

// Estimate token count for content using simple heuristics
int estimate_content_tokens(const char *content) {
    if (content == NULL) return 0;
    
    int char_count = strlen(content);
    float chars_per_token = 5.5f; // Default for modern tokenizers
    
    // Adjust estimation based on content type
    // Code and structured text are more efficiently tokenized
    if (strstr(content, "function ") != NULL || strstr(content, "def ") != NULL ||
        strstr(content, "#include") != NULL || strstr(content, "class ") != NULL) {
        chars_per_token *= 1.2f; // Code is ~20% more efficient
    }
    
    // JSON content is very efficiently tokenized
    if (content[0] == '{' || strstr(content, "\"role\":") != NULL) {
        chars_per_token *= 1.3f; // JSON is ~30% more efficient
    }
    
    return (int)ceil(char_count / chars_per_token);
}

// Smart truncate content preserving structure
int smart_truncate_content(const char *content, int max_tokens, 
                          char **truncated_content, int *was_truncated) {
    if (content == NULL || truncated_content == NULL) return -1;
    
    int total_tokens = estimate_content_tokens(content);
    if (was_truncated != NULL) {
        *was_truncated = (total_tokens > max_tokens);
    }
    
    // If content fits within budget, return as-is
    if (max_tokens <= 0 || total_tokens <= max_tokens) {
        *truncated_content = safe_strdup(content);
        return (*truncated_content != NULL) ? 0 : -1;
    }
    
    // Calculate target character count
    float chars_per_token = 5.5f;
    int target_chars = (int)(max_tokens * chars_per_token * 0.8f); // Leave some buffer
    int content_len = strlen(content);
    
    if (target_chars >= content_len) {
        *truncated_content = safe_strdup(content);
        return (*truncated_content != NULL) ? 0 : -1;
    }
    
    // Try to find a good truncation point
    int best_cut = target_chars;
    
    // Look for natural break points: function ends, class ends, blank lines
    for (int i = target_chars - 200; i >= 0 && i > target_chars - 500; i--) {
        if (content[i] == '\n') {
            // Check if this looks like a function or class boundary
            const char *line_start = &content[i + 1];
            
            // Look for function definitions starting on next line
            if (strncmp(line_start, "int ", 4) == 0 || 
                strncmp(line_start, "void ", 5) == 0 ||
                strncmp(line_start, "char", 4) == 0 ||
                strncmp(line_start, "static ", 7) == 0 ||
                strncmp(line_start, "typedef ", 8) == 0 ||
                line_start[0] == '}' || 
                (line_start[0] == '\n' && line_start[1] != '\0')) {
                best_cut = i;
                break;
            }
        }
    }
    
    // Allocate result buffer with truncation notice
    const char *truncation_notice = "\n\n[... Content truncated to fit token budget ...]";
    int notice_len = strlen(truncation_notice);
    char *result = malloc(best_cut + notice_len + 1);
    if (result == NULL) return -1;
    
    memcpy(result, content, best_cut);
    strcpy(result + best_cut, truncation_notice);
    
    *truncated_content = result;
    return 0;
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

// Read file content with token budget awareness
FileErrorCode file_read_content_smart(const char *file_path, int start_line, int end_line, 
                                     int max_tokens, char **content, int *truncated) {
    if (file_path == NULL || content == NULL) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    // First, read the content normally
    char *raw_content = NULL;
    FileErrorCode result = file_read_content(file_path, start_line, end_line, &raw_content);
    
    if (result != FILE_SUCCESS) {
        return result;
    }
    
    // If no token limit specified, return as-is
    if (max_tokens <= 0) {
        *content = raw_content;
        if (truncated != NULL) *truncated = 0;
        return FILE_SUCCESS;
    }
    
    // Apply smart truncation if needed
    char *smart_content = NULL;
    int was_truncated = 0;
    
    if (smart_truncate_content(raw_content, max_tokens, &smart_content, &was_truncated) != 0) {
        free(raw_content);
        return FILE_ERROR_MEMORY;
    }
    
    free(raw_content);
    *content = smart_content;
    if (truncated != NULL) *truncated = was_truncated;
    
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
    if (directory_path == NULL || listing == NULL) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    if (!file_validate_path(directory_path)) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    memset(listing, 0, sizeof(DirectoryListing));
    
    // Allocate initial space for entries
    int capacity = 100;
    listing->entries = malloc(capacity * sizeof(DirectoryEntry));
    if (listing->entries == NULL) {
        return FILE_ERROR_MEMORY;
    }
    
    if (recursive) {
        // Use recursive helper function
        FileErrorCode result = list_directory_recursive(directory_path, pattern, include_hidden, listing, &capacity);
        if (result != FILE_SUCCESS) {
            cleanup_directory_listing(listing);
            return result;
        }
        return FILE_SUCCESS;
    }
    
    // Non-recursive listing (existing implementation)
    DIR *dir = opendir(directory_path);
    if (dir == NULL) {
        free(listing->entries);
        listing->entries = NULL;
        return (errno == ENOENT) ? FILE_ERROR_NOT_FOUND : FILE_ERROR_PERMISSION;
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

// Helper function to match file patterns (supports basic wildcards)
static int matches_file_pattern(const char *filename, const char *pattern) {
    if (pattern == NULL || filename == NULL) {
        return 1; // No pattern means match everything
    }
    
    // Simple pattern matching with * and ? wildcards
    const char *f = filename;
    const char *p = pattern;
    
    while (*f && *p) {
        if (*p == '*') {
            // Skip consecutive asterisks
            while (*p == '*') p++;
            if (!*p) return 1; // Pattern ends with *, match rest
            
            // Try to match the rest of the pattern at each position
            while (*f) {
                if (matches_file_pattern(f, p)) {
                    return 1;
                }
                f++;
            }
            return 0;
        } else if (*p == '?' || *p == *f) {
            // ? matches any character, or exact match
            p++;
            f++;
        } else {
            return 0; // No match
        }
    }
    
    // Skip trailing asterisks
    while (*p == '*') p++;
    
    // Both should be at the end for a complete match
    return (*f == 0 && *p == 0);
}

// Helper function for recursive directory traversal
static FileErrorCode list_directory_recursive(const char *directory_path, const char *pattern,
                                             int include_hidden, DirectoryListing *listing, int *capacity) {
    DIR *dir = opendir(directory_path);
    if (dir == NULL) {
        return (errno == ENOENT) ? FILE_ERROR_NOT_FOUND : FILE_ERROR_PERMISSION;
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
        if (listing->count >= *capacity) {
            *capacity *= 2;
            DirectoryEntry *new_entries = realloc(listing->entries, *capacity * sizeof(DirectoryEntry));
            if (new_entries == NULL) {
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
            closedir(dir);
            return FILE_ERROR_MEMORY;
        }
        
        listing->count++;
        
        // If this is a directory, recurse into it
        if (dir_entry->is_directory && listing->count < FILE_MAX_LIST_ENTRIES) {
            FileErrorCode recursive_result = list_directory_recursive(dir_entry->full_path, pattern, include_hidden, listing, capacity);
            if (recursive_result != FILE_SUCCESS) {
                closedir(dir);
                return recursive_result;
            }
        }
    }
    
    closedir(dir);
    return FILE_SUCCESS;
}

// Helper function to search content in a single file
// Returns FILE_SUCCESS even when file is skipped (too large, binary, etc.)
// Only returns errors for actual failures that should abort the search
static FileErrorCode search_file_content(const char *file_path, const char *pattern,
                                        int case_sensitive, SearchResults *results, int *capacity) {
    if (file_path == NULL || pattern == NULL || results == NULL || capacity == NULL) {
        return FILE_ERROR_INVALID_PATH;
    }

    // Check file size first - skip files that are too large
    struct stat file_stat;
    if (stat(file_path, &file_stat) != 0) {
        // Can't stat file, skip it silently
        return FILE_SUCCESS;
    }

    if (file_stat.st_size > FILE_SEARCH_MAX_SIZE) {
        // File too large, skip silently
        return FILE_SUCCESS;
    }

    // Skip empty files
    if (file_stat.st_size == 0) {
        return FILE_SUCCESS;
    }

    // Check for binary extension first (fast check)
    const char *basename = strrchr(file_path, '/');
    basename = basename ? basename + 1 : file_path;
    if (has_binary_extension(basename)) {
        // Binary file by extension, skip silently
        return FILE_SUCCESS;
    }

    // Check if file content appears to be binary
    if (is_binary_content(file_path)) {
        // Binary content detected, skip silently
        return FILE_SUCCESS;
    }

    char *content = NULL;
    FileErrorCode read_result = file_read_content(file_path, 0, 0, &content);
    if (read_result != FILE_SUCCESS) {
        // Failed to read file (permission denied, too large, etc.)
        // Skip silently and continue with other files
        return FILE_SUCCESS;
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
                                            const char *file_pattern, int recursive, int case_sensitive,
                                            SearchResults *results, int *capacity) {
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        // If we can't open the directory, skip it silently and continue
        // Only fail for the root search path, not subdirectories
        return FILE_SUCCESS;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && results->count < FILE_MAX_SEARCH_RESULTS) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Skip hidden files and directories (starting with .)
        if (entry->d_name[0] == '.') {
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
            // Check if file matches the file pattern
            if (!matches_file_pattern(entry->d_name, file_pattern)) {
                continue; // Skip files that don't match the pattern
            }

            // Regular file - search its content
            // search_file_content now handles large/binary files gracefully
            FileErrorCode search_result = search_file_content(full_path, pattern, case_sensitive, results, capacity);
            if (search_result == FILE_ERROR_MEMORY) {
                // Only abort on memory errors
                closedir(dir);
                return search_result;
            }
            results->files_searched++;
        } else if (S_ISDIR(entry_stat.st_mode) && recursive) {
            // Check if this is a directory we should skip
            if (should_skip_directory(entry->d_name)) {
                continue;
            }

            // Directory - recurse if requested
            FileErrorCode search_result = search_directory_content(full_path, pattern, file_pattern, recursive, case_sensitive, results, capacity);
            if (search_result == FILE_ERROR_MEMORY) {
                // Only abort on memory errors
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
    if (search_path == NULL || pattern == NULL || results == NULL) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    if (!file_validate_path(search_path)) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    memset(results, 0, sizeof(SearchResults));
    
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
        return search_directory_content(search_path, pattern, file_pattern, recursive, case_sensitive, results, &capacity);
    }
    
    // Search in single file - check if it matches the file pattern
    const char *filename = strrchr(search_path, '/');
    filename = filename ? filename + 1 : search_path; // Get basename

    if (!matches_file_pattern(filename, file_pattern)) {
        // File doesn't match pattern, return empty results
        results->results = malloc(sizeof(SearchResult)); // Allocate minimal space
        if (results->results == NULL) {
            return FILE_ERROR_MEMORY;
        }
        return FILE_SUCCESS;
    }

    // Skip binary files by extension
    if (has_binary_extension(filename)) {
        results->results = malloc(sizeof(SearchResult));
        if (results->results == NULL) {
            return FILE_ERROR_MEMORY;
        }
        return FILE_SUCCESS;
    }

    // Check file size
    if (path_stat.st_size > FILE_SEARCH_MAX_SIZE) {
        // File too large, return empty results instead of error
        results->results = malloc(sizeof(SearchResult));
        if (results->results == NULL) {
            return FILE_ERROR_MEMORY;
        }
        return FILE_SUCCESS;
    }

    // Skip binary content
    if (is_binary_content(search_path)) {
        results->results = malloc(sizeof(SearchResult));
        if (results->results == NULL) {
            return FILE_ERROR_MEMORY;
        }
        return FILE_SUCCESS;
    }

    char *content = NULL;
    FileErrorCode read_result = file_read_content(search_path, 0, 0, &content);
    if (read_result != FILE_SUCCESS) {
        // For single file search, return the actual error
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

// Smart search with token budget awareness
FileErrorCode file_search_content_smart(const char *search_path, const char *pattern,
                                       const char *file_pattern, int recursive, 
                                       int case_sensitive, int max_tokens, 
                                       int max_results, SearchResults *results) {
    // First, perform normal search
    FileErrorCode result = file_search_content(search_path, pattern, file_pattern, 
                                              recursive, case_sensitive, results);
    
    if (result != FILE_SUCCESS) {
        return result;
    }
    
    // If no limits specified, return as-is
    if (max_tokens <= 0 && max_results <= 0) {
        return FILE_SUCCESS;
    }
    
    // Apply result limiting based on token budget
    int token_budget = max_tokens;
    int result_limit = (max_results > 0) ? max_results : results->count;
    
    // Estimate tokens for each result and trim if needed
    int kept_results = 0;
    int estimated_tokens = 0;
    
    for (int i = 0; i < results->count && kept_results < result_limit; i++) {
        SearchResult *search_result = &results->results[i];
        
        // Estimate tokens for this result
        int result_tokens = 0;
        if (search_result->file_path) result_tokens += estimate_content_tokens(search_result->file_path);
        if (search_result->line_content) result_tokens += estimate_content_tokens(search_result->line_content);
        if (search_result->match_context) result_tokens += estimate_content_tokens(search_result->match_context);
        result_tokens += 10; // Overhead for JSON structure
        
        // Check if adding this result would exceed budget
        if (max_tokens > 0 && (estimated_tokens + result_tokens) > token_budget) {
            break;
        }
        
        // If we're keeping fewer results, move this one forward
        if (kept_results != i) {
            results->results[kept_results] = results->results[i];
            // Clear the moved-from slot to avoid double-free
            memset(&results->results[i], 0, sizeof(SearchResult));
        }
        
        kept_results++;
        estimated_tokens += result_tokens;
    }
    
    // Clean up any results we're not keeping
    for (int i = kept_results; i < results->count; i++) {
        free(results->results[i].file_path);
        free(results->results[i].line_content);
        free(results->results[i].match_context);
    }
    
    // Update result count
    results->count = kept_results;
    
    // Shrink the results array if we removed many results
    if (kept_results < results->count / 2) {
        SearchResult *new_results = realloc(results->results, kept_results * sizeof(SearchResult));
        if (new_results != NULL) {
            results->results = new_results;
        }
    }
    
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
    
    // First pass: calculate the actual string length after unescaping
    const char *src = start;
    size_t actual_len = 0;
    while (*src != '\0' && *src != '"') {
        if (*src == '\\' && *(src + 1) != '\0') {
            src += 2; // Skip escape sequence
            actual_len++;
        } else {
            src++;
            actual_len++;
        }
    }
    
    if (*src != '"') return NULL;
    
    // Allocate result buffer
    char *result = malloc(actual_len + 1);
    if (result == NULL) return NULL;
    
    // Second pass: copy and unescape
    src = start;
    char *dst = result;
    while (*src != '\0' && *src != '"') {
        if (*src == '\\' && *(src + 1) != '\0') {
            src++; // Skip backslash
            switch (*src) {
                case '"': *dst++ = '"'; break;
                case '\\': *dst++ = '\\'; break;
                case 'n': *dst++ = '\n'; break;
                case 'r': *dst++ = '\r'; break;
                case 't': *dst++ = '\t'; break;
                case 'b': *dst++ = '\b'; break;
                case 'f': *dst++ = '\f'; break;
                default: *dst++ = *src; break; // Unknown escape, copy as-is
            }
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    
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
    
    char *file_path = extract_string_param(tool_call->arguments, "file_path");
    char *content = extract_string_param(tool_call->arguments, "content");
    int create_backup = extract_bool_param(tool_call->arguments, "create_backup", 0);
    
    if (file_path == NULL || content == NULL) {
        result->result = safe_strdup("Error: Missing required parameters 'file_path' or 'content'");
        result->success = 0;
        free(file_path);
        free(content);
        return 0;
    }
    
    // Count lines in content for user visibility
    int line_count = 1;
    size_t content_len = strlen(content);
    for (size_t i = 0; i < content_len; i++) {
        if (content[i] == '\n') line_count++;
    }
    
    // Log user-visible information about the write operation
    print_tool_box_line("Writing to file: %s", file_path);
    print_tool_box_line("  %d lines (%zu bytes)%s",
           line_count, content_len, create_backup ? " [with backup]" : "");

    FileErrorCode error = file_write_content(file_path, content, create_backup);

    char result_msg[512];
    if (error == FILE_SUCCESS) {
        print_tool_box_line("  File written successfully");

        snprintf(result_msg, sizeof(result_msg),
            "{\"success\": true, \"file_path\": \"%s\", \"lines_written\": %d, \"bytes_written\": %zu, \"backup_created\": %s}",
            file_path, line_count, content_len, create_backup ? "true" : "false");
        result->success = 1;
    } else {
        print_tool_box_line("  Error: %s", file_error_message(error));
        
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
    
    // Count lines in content for user visibility
    int line_count = 1;
    size_t content_len = strlen(content);
    for (size_t i = 0; i < content_len; i++) {
        if (content[i] == '\n') line_count++;
    }
    
    // Log user-visible information about the append operation
    print_tool_box_line("Appending to file: %s", file_path);
    print_tool_box_line("  Adding %d lines (%zu bytes)", line_count, content_len);

    FileErrorCode error = file_append_content(file_path, content);

    char result_msg[512];
    if (error == FILE_SUCCESS) {
        print_tool_box_line("  Content appended successfully");

        snprintf(result_msg, sizeof(result_msg),
            "{\"success\": true, \"file_path\": \"%s\", \"lines_appended\": %d, \"bytes_appended\": %zu}",
            file_path, line_count, content_len);
        result->success = 1;
    } else {
        print_tool_box_line("  Error: %s", file_error_message(error));
        
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
    
    // Log user-visible information about the directory listing operation
    print_tool_box_line("Listing directory: %s", directory_path);
    if (pattern != NULL) {
        print_tool_box_line("  Pattern filter: %s", pattern);
    }
    if (include_hidden) {
        print_tool_box_line("  Including hidden files");
    }

    DirectoryListing listing;
    FileErrorCode error = file_list_directory(directory_path, pattern, include_hidden, 0, &listing);

    if (error == FILE_SUCCESS) {
        // Show user-friendly summary
        print_tool_box_line("  Found %d entries (%d files, %d directories)",
               listing.count, listing.total_files, listing.total_directories);

        // Build JSON response using cJSON for safety
        cJSON *response = cJSON_CreateObject();
        if (response != NULL) {
            if (!cJSON_AddBoolToObject(response, "success", 1)) {
                cJSON_Delete(response);
                result->result = safe_strdup("{\"success\":false,\"error\":\"Memory allocation failed\"}");
                result->success = 0;
                cleanup_directory_listing(&listing);
                free(directory_path);
                free(pattern);
                return 0;
            }

            cJSON *entries = cJSON_CreateArray();
            if (entries != NULL) {
                for (int i = 0; i < listing.count; i++) {
                    cJSON *entry = cJSON_CreateObject();
                    if (entry != NULL) {
                        // Check return values - on failure, skip this entry
                        if (!cJSON_AddStringToObject(entry, "name", listing.entries[i].name) ||
                            !cJSON_AddStringToObject(entry, "full_path", listing.entries[i].full_path) ||
                            !cJSON_AddBoolToObject(entry, "is_directory", listing.entries[i].is_directory) ||
                            !cJSON_AddNumberToObject(entry, "size", (double)listing.entries[i].size)) {
                            cJSON_Delete(entry);
                            continue;
                        }
                        cJSON_AddItemToArray(entries, entry);
                    }
                }
                cJSON_AddItemToObject(response, "entries", entries);
            }

            cJSON_AddNumberToObject(response, "total_files", listing.total_files);
            cJSON_AddNumberToObject(response, "total_directories", listing.total_directories);
            cJSON_AddNumberToObject(response, "total_entries", listing.count);

            result->result = cJSON_PrintUnformatted(response);
            result->success = 1;
            cJSON_Delete(response);
        } else {
            result->result = safe_strdup("{\"success\":false,\"error\":\"Memory allocation failed\"}");
            result->success = 0;
        }

        cleanup_directory_listing(&listing);
    } else {
        print_tool_box_line("  Error: %s", file_error_message(error));

        // Use cJSON for error response to handle special character escaping
        cJSON *err_response = cJSON_CreateObject();
        if (err_response != NULL) {
            cJSON_AddBoolToObject(err_response, "success", 0);
            cJSON_AddStringToObject(err_response, "error", file_error_message(error));
            cJSON_AddStringToObject(err_response, "directory_path", directory_path);
            result->result = cJSON_PrintUnformatted(err_response);
            cJSON_Delete(err_response);
        } else {
            result->result = safe_strdup("{\"success\":false,\"error\":\"Memory allocation failed\"}");
        }
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
    int max_tokens = extract_int_param(tool_call->arguments, "max_tokens", 0);
    int max_results = extract_int_param(tool_call->arguments, "max_results", 0);
    // Default to recursive search (1) for directories - this is the expected behavior
    int recursive = extract_bool_param(tool_call->arguments, "recursive", 1);
    char *file_pattern = extract_string_param(tool_call->arguments, "file_pattern");

    // Log user-visible information about the search operation
    print_tool_box_line("Searching for pattern: \"%s\"", pattern);
    print_tool_box_line("  Search path: %s", search_path);
    print_tool_box_line("  Case sensitive: %s", case_sensitive ? "yes" : "no");
    print_tool_box_line("  Recursive: %s", recursive ? "yes" : "no");
    if (file_pattern != NULL) {
        print_tool_box_line("  File pattern: %s", file_pattern);
    }
    if (max_results > 0) {
        print_tool_box_line("  Max results: %d", max_results);
    }
    if (max_tokens > 0) {
        print_tool_box_line("  Token limit: %d", max_tokens);
    }

    SearchResults search_results;
    FileErrorCode error;

    // Use smart search if limits are specified
    if (max_tokens > 0 || max_results > 0) {
        error = file_search_content_smart(search_path, pattern, file_pattern, recursive, case_sensitive,
                                        max_tokens, max_results, &search_results);
    } else {
        error = file_search_content(search_path, pattern, file_pattern, recursive, case_sensitive, &search_results);
    }

    free(file_pattern);
    
    if (error == FILE_SUCCESS) {
        // Show user-friendly summary
        print_tool_box_line("  Found %d matches in %d files",
               search_results.total_matches, search_results.files_searched);

        // Build JSON response using cJSON for safety
        cJSON *response = cJSON_CreateObject();
        if (response != NULL) {
            if (!cJSON_AddBoolToObject(response, "success", 1)) {
                cJSON_Delete(response);
                result->result = safe_strdup("{\"success\":false,\"error\":\"Memory allocation failed\"}");
                result->success = 0;
                cleanup_search_results(&search_results);
                free(search_path);
                free(pattern);
                return 0;
            }

            cJSON *matches = cJSON_CreateArray();
            if (matches != NULL) {
                for (int i = 0; i < search_results.count; i++) {
                    cJSON *match = cJSON_CreateObject();
                    if (match != NULL) {
                        // Check return values - on failure, skip this match
                        if (!cJSON_AddStringToObject(match, "file", search_results.results[i].file_path) ||
                            !cJSON_AddNumberToObject(match, "line", search_results.results[i].line_number) ||
                            !cJSON_AddStringToObject(match, "content", search_results.results[i].line_content)) {
                            cJSON_Delete(match);
                            continue;
                        }
                        cJSON_AddItemToArray(matches, match);
                    }
                }
                cJSON_AddItemToObject(response, "matches", matches);
            }

            cJSON_AddNumberToObject(response, "total_matches", search_results.total_matches);
            cJSON_AddNumberToObject(response, "files_searched", search_results.files_searched);

            result->result = cJSON_PrintUnformatted(response);
            result->success = 1;
            cJSON_Delete(response);
        } else {
            result->result = safe_strdup("{\"success\":false,\"error\":\"Memory allocation failed\"}");
            result->success = 0;
        }

        cleanup_search_results(&search_results);
    } else {
        print_tool_box_line("  Error: %s", file_error_message(error));

        // Use cJSON for error response to handle special character escaping
        cJSON *err_response = cJSON_CreateObject();
        if (err_response != NULL) {
            cJSON_AddBoolToObject(err_response, "success", 0);
            cJSON_AddStringToObject(err_response, "error", file_error_message(error));
            cJSON_AddStringToObject(err_response, "search_path", search_path);
            result->result = cJSON_PrintUnformatted(err_response);
            cJSON_Delete(err_response);
        } else {
            result->result = safe_strdup("{\"success\":false,\"error\":\"Memory allocation failed\"}");
        }
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
    
    // Log user-visible information about the info operation
    print_tool_box_line("Getting file info: %s", file_path);

    FileInfo info;
    FileErrorCode error = file_get_info(file_path, &info);

    if (error == FILE_SUCCESS) {
        // Show user-friendly summary
        print_tool_box_line("  %s (%ld bytes)",
               info.is_directory ? "Directory" : "File", info.size);
        print_tool_box_line("  Permissions: %s%s%s",
               info.is_readable ? "r" : "-",
               info.is_writable ? "w" : "-",
               info.is_executable ? "x" : "-");

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
        print_tool_box_line("  Error: %s", file_error_message(error));
        
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
    
    // Log user-visible information about the file read operation
    print_tool_box_line("Reading file: %s", file_path);

    int start_line = extract_int_param(tool_call->arguments, "start_line", 0);
    int end_line = extract_int_param(tool_call->arguments, "end_line", 0);
    int max_tokens = extract_int_param(tool_call->arguments, "max_tokens", 0);

    // Show range information if specified
    if (start_line > 0 && end_line > 0) {
        print_tool_box_line("  Range: lines %d-%d", start_line, end_line);
    } else if (start_line > 0) {
        print_tool_box_line("  Range: from line %d", start_line);
    }

    if (max_tokens > 0) {
        print_tool_box_line("  Token limit: %d (smart truncation enabled)", max_tokens);
    }
    
    char *content = NULL;
    int was_truncated = 0;
    FileErrorCode error;
    
    // Use smart reading if max_tokens is specified
    if (max_tokens > 0) {
        error = file_read_content_smart(file_path, start_line, end_line, max_tokens, &content, &was_truncated);
    } else {
        error = file_read_content(file_path, start_line, end_line, &content);
    }
    
    if (error == FILE_SUCCESS && content != NULL) {
        // Count lines in the content for user visibility
        int line_count = 1;
        for (const char *p = content; *p; p++) {
            if (*p == '\n') line_count++;
        }
        
        // Show user-friendly summary
        print_tool_box_line("  Read %d lines (%zu bytes)%s",
               line_count, strlen(content), was_truncated ? " [truncated]" : "");
        
        // Escape content for JSON
        char *escaped_content = json_escape_string(content);
        char *escaped_file_path = json_escape_string(file_path);
        
        if (escaped_content != NULL && escaped_file_path != NULL) {
            // Format as JSON result  
            size_t result_size = strlen(escaped_content) + strlen(escaped_file_path) + 200;
            char *json_result = malloc(result_size);
            if (json_result != NULL) {
                snprintf(json_result, result_size, 
                    "{\"success\": true, \"file_path\": \"%s\", \"content\": \"%s\", \"lines_read\": %d, \"truncated\": %s}",
                    escaped_file_path, escaped_content, line_count, 
                    was_truncated ? "true" : "false");
                result->result = json_result;
                result->success = 1;
            } else {
                result->result = safe_strdup("Error: Memory allocation failed");
                result->success = 0;
            }
        } else {
            result->result = safe_strdup("Error: JSON escaping failed");
            result->success = 0;
        }
        
        free(escaped_content);
        free(escaped_file_path);
        free(content);
    } else {
        print_tool_box_line("  Error: %s", file_error_message(error));

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


// Helper function to split content into lines
char** split_lines(const char *content, int *line_count) {
    if (content == NULL || line_count == NULL) {
        if (line_count) *line_count = 0;
        return NULL;
    }
    
    // Count lines first
    int count = 1;
    for (const char *p = content; *p; p++) {
        if (*p == '\n') count++;
    }
    
    // Handle empty content or content ending with newline
    if (strlen(content) == 0) {
        count = 0;
    } else if (content[strlen(content) - 1] == '\n') {
        count--; // Don't count empty line after final newline
    }
    
    // For empty content, return NULL
    if (count == 0) {
        *line_count = 0;
        return NULL;
    }
    
    char **lines = malloc(count * sizeof(char*));
    if (lines == NULL) {
        *line_count = 0;
        return NULL;
    }
    
    // Split into lines
    int line_idx = 0;
    const char *start = content;
    const char *end = content;
    
    while (line_idx < count && *end) {
        // Find end of current line
        while (*end && *end != '\n') end++;
        
        // Copy line (without newline)
        int len = end - start;
        lines[line_idx] = malloc(len + 1);
        if (lines[line_idx] == NULL) {
            // Cleanup on failure
            for (int i = 0; i < line_idx; i++) {
                free(lines[i]);
            }
            free(lines);
            *line_count = 0;
            return NULL;
        }
        
        if (len > 0) {
            memcpy(lines[line_idx], start, len);
        }
        lines[line_idx][len] = '\0';
        line_idx++;
        
        // Move to next line
        if (*end == '\n') end++;
        start = end;
    }
    
    *line_count = count;
    return lines;
}

// Helper function to join lines back into content
char* join_lines(char **lines, int line_count) {
    if (lines == NULL || line_count <= 0) {
        return safe_strdup("");
    }
    
    // Calculate total size needed
    size_t total_size = 1; // For null terminator
    for (int i = 0; i < line_count; i++) {
        if (lines[i] != NULL) {
            total_size += strlen(lines[i]) + 1; // +1 for newline
        }
    }
    
    char *result = malloc(total_size);
    if (result == NULL) return NULL;
    
    result[0] = '\0';
    for (int i = 0; i < line_count; i++) {
        if (lines[i] != NULL) {
            strcat(result, lines[i]);
            if (i < line_count - 1 || strlen(lines[i]) > 0) {
                strcat(result, "\n");
            }
        }
    }
    
    return result;
}

// Clean up delta patch structure
void cleanup_delta_patch(DeltaPatch *patch) {
    if (patch == NULL) return;
    
    if (patch->operations != NULL) {
        for (int i = 0; i < patch->num_operations; i++) {
            DeltaOperation *op = &patch->operations[i];
            
            if (op->lines != NULL) {
                for (int j = 0; j < op->num_lines; j++) {
                    free(op->lines[j]);
                }
                free(op->lines);
            }
            
            free(op->context_before);
            free(op->context_after);
        }
        free(patch->operations);
    }
    
    free(patch->original_checksum);
    memset(patch, 0, sizeof(DeltaPatch));
}

// Apply delta patch to file
FileErrorCode file_apply_delta(const char *file_path, const DeltaPatch *patch) {
    if (file_path == NULL || patch == NULL) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    if (!file_validate_path(file_path)) {
        return FILE_ERROR_INVALID_PATH;
    }
    
    // Read current file content
    char *original_content = NULL;
    FileErrorCode read_result = file_read_content(file_path, 0, 0, &original_content);
    if (read_result != FILE_SUCCESS) {
        return read_result;
    }
    
    // Split original content into lines
    int original_line_count = 0;
    char **original_lines = split_lines(original_content, &original_line_count);
    free(original_content);
    
    if (original_lines == NULL && original_line_count > 0) {
        return FILE_ERROR_MEMORY;
    }
    
    // Create backup if requested
    if (patch->create_backup) {
        char *backup_path = NULL;
        FileErrorCode backup_result = file_create_backup(file_path, &backup_path);
        if (backup_result != FILE_SUCCESS) {
            // Cleanup
            for (int i = 0; i < original_line_count; i++) {
                free(original_lines[i]);
            }
            free(original_lines);
            return backup_result;
        }
        free(backup_path);
    }
    
    // Apply operations in order
    for (int op_idx = 0; op_idx < patch->num_operations; op_idx++) {
        const DeltaOperation *op = &patch->operations[op_idx];
        
        // Validate line numbers
        if (op->start_line < 1 || op->start_line > original_line_count + 1) {
            // Cleanup
            for (int i = 0; i < original_line_count; i++) {
                free(original_lines[i]);
            }
            free(original_lines);
            return FILE_ERROR_INVALID_PATH;
        }
        
        // Convert to 0-based indexing
        int start_idx = op->start_line - 1;
        
        switch (op->type) {
            case DELTA_INSERT: {
                if (op->lines == NULL || op->num_lines <= 0) break;
                
                // Create new lines array with extra space
                int new_line_count = original_line_count + op->num_lines;
                char **new_lines = malloc(new_line_count * sizeof(char*));
                if (new_lines == NULL) {
                    // Cleanup
                    for (int i = 0; i < original_line_count; i++) {
                        free(original_lines[i]);
                    }
                    free(original_lines);
                    return FILE_ERROR_MEMORY;
                }
                
                // Copy lines before insertion point
                for (int i = 0; i < start_idx; i++) {
                    new_lines[i] = original_lines[i];
                }
                
                // Insert new lines
                for (int i = 0; i < op->num_lines; i++) {
                    new_lines[start_idx + i] = safe_strdup(op->lines[i]);
                    if (new_lines[start_idx + i] == NULL) {
                        // Cleanup on failure
                        for (int j = 0; j < start_idx + i; j++) {
                            free(new_lines[j]);
                        }
                        for (int j = start_idx; j < original_line_count; j++) {
                            free(original_lines[j]);
                        }
                        free(new_lines);
                        free(original_lines);
                        return FILE_ERROR_MEMORY;
                    }
                }
                
                // Copy lines after insertion point
                for (int i = start_idx; i < original_line_count; i++) {
                    new_lines[i + op->num_lines] = original_lines[i];
                }
                
                // Update arrays
                free(original_lines);
                original_lines = new_lines;
                original_line_count = new_line_count;
                break;
            }
            
            case DELTA_DELETE: {
                int end_idx = start_idx + op->line_count;
                if (end_idx > original_line_count) {
                    end_idx = original_line_count;
                }
                
                // Free deleted lines
                for (int i = start_idx; i < end_idx; i++) {
                    free(original_lines[i]);
                }
                
                // Shift remaining lines
                int deleted_count = end_idx - start_idx;
                for (int i = start_idx; i < original_line_count - deleted_count; i++) {
                    original_lines[i] = original_lines[i + deleted_count];
                }
                
                original_line_count -= deleted_count;
                break;
            }
            
            case DELTA_REPLACE: {
                int end_idx = start_idx + op->line_count;
                if (end_idx > original_line_count) {
                    end_idx = original_line_count;
                }
                
                // Free original lines in range
                for (int i = start_idx; i < end_idx; i++) {
                    free(original_lines[i]);
                }
                
                int replaced_count = end_idx - start_idx;
                int size_diff = op->num_lines - replaced_count;
                
                if (size_diff != 0) {
                    // Need to resize array
                    int new_line_count = original_line_count + size_diff;
                    char **new_lines = malloc(new_line_count * sizeof(char*));
                    if (new_lines == NULL) {
                        // Cleanup
                        for (int i = 0; i < start_idx; i++) {
                            free(original_lines[i]);
                        }
                        for (int i = end_idx; i < original_line_count; i++) {
                            free(original_lines[i]);
                        }
                        free(original_lines);
                        return FILE_ERROR_MEMORY;
                    }
                    
                    // Copy lines before replacement
                    for (int i = 0; i < start_idx; i++) {
                        new_lines[i] = original_lines[i];
                    }
                    
                    // Copy lines after replacement
                    for (int i = end_idx; i < original_line_count; i++) {
                        new_lines[i + size_diff] = original_lines[i];
                    }
                    
                    free(original_lines);
                    original_lines = new_lines;
                    original_line_count = new_line_count;
                }
                
                // Insert replacement lines
                for (int i = 0; i < op->num_lines; i++) {
                    original_lines[start_idx + i] = safe_strdup(op->lines[i]);
                    if (original_lines[start_idx + i] == NULL) {
                        // Cleanup on failure
                        for (int j = 0; j < original_line_count; j++) {
                            if (j < start_idx || j >= start_idx + op->num_lines) {
                                free(original_lines[j]);
                            }
                        }
                        free(original_lines);
                        return FILE_ERROR_MEMORY;
                    }
                }
                break;
            }
        }
    }
    
    // Join lines back into content
    char *new_content = join_lines(original_lines, original_line_count);
    
    // Cleanup lines array
    for (int i = 0; i < original_line_count; i++) {
        free(original_lines[i]);
    }
    free(original_lines);
    
    if (new_content == NULL) {
        return FILE_ERROR_MEMORY;
    }
    
    // Write modified content back to file
    FileErrorCode write_result = file_write_content(file_path, new_content, 0);
    free(new_content);
    
    return write_result;
}

// Execute file delta tool call
int execute_file_delta_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    // Initialize result structure
    result->tool_call_id = NULL;
    result->result = NULL;
    result->success = 0;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    // TODO: Parse JSON arguments to extract file_path and delta operations
    // For now, return an error indicating implementation is needed
    char result_msg[256] = {0}; // Initialize array
    snprintf(result_msg, sizeof(result_msg),
        "{\"success\": false, \"error\": \"Delta tool implementation in progress\"}");
    
    result->result = safe_strdup(result_msg);
    result->success = 0;
    
    return (result->result != NULL) ? 0 : -1;
}

// Register all file tools
int register_file_tools(ToolRegistry *registry) {
    if (registry == NULL) return -1;
    
    // 1. Register file_read tool
    ToolParameter read_parameters[4];
    
    // Parameter 1: file_path (required)
    read_parameters[0].name = strdup("file_path");
    read_parameters[0].type = strdup("string");
    read_parameters[0].description = strdup("Path to the file to read");
    read_parameters[0].enum_values = NULL;
    read_parameters[0].enum_count = 0;
    read_parameters[0].required = 1;
    
    // Parameter 2: start_line (optional)
    read_parameters[1].name = strdup("start_line");
    read_parameters[1].type = strdup("number");
    read_parameters[1].description = strdup("Starting line number (1-based, 0 for entire file)");
    read_parameters[1].enum_values = NULL;
    read_parameters[1].enum_count = 0;
    read_parameters[1].required = 0;
    
    // Parameter 3: end_line (optional)
    read_parameters[2].name = strdup("end_line");
    read_parameters[2].type = strdup("number");
    read_parameters[2].description = strdup("Ending line number (1-based, 0 for to end of file)");
    read_parameters[2].enum_values = NULL;
    read_parameters[2].enum_count = 0;
    read_parameters[2].required = 0;
    
    // Parameter 4: max_tokens (optional)
    read_parameters[3].name = strdup("max_tokens");
    read_parameters[3].type = strdup("number");
    read_parameters[3].description = strdup("Maximum tokens to return (0 for no limit, enables smart truncation)");
    read_parameters[3].enum_values = NULL;
    read_parameters[3].enum_count = 0;
    read_parameters[3].required = 0;
    
    // Check for allocation failures
    for (int i = 0; i < 4; i++) {
        if (read_parameters[i].name == NULL || 
            read_parameters[i].type == NULL ||
            read_parameters[i].description == NULL) {
            // Cleanup on failure
            for (int j = 0; j <= i; j++) {
                free(read_parameters[j].name);
                free(read_parameters[j].type);
                free(read_parameters[j].description);
            }
            return -1;
        }
    }
    
    // Register the tool using the new system
    int result = register_tool(registry, "file_read", 
                              "Read file contents with optional line range and smart truncation",
                              read_parameters, 4, execute_file_read_tool_call);
    
    // Clean up temporary parameter storage
    for (int i = 0; i < 4; i++) {
        free(read_parameters[i].name);
        free(read_parameters[i].type);
        free(read_parameters[i].description);
    }
    
    if (result != 0) return -1;
    
    // 2. Register file_write tool
    ToolParameter write_parameters[3];
    
    // Parameter 1: file_path (required)
    write_parameters[0].name = strdup("file_path");
    write_parameters[0].type = strdup("string");
    write_parameters[0].description = strdup("Path to the file to write");
    write_parameters[0].enum_values = NULL;
    write_parameters[0].enum_count = 0;
    write_parameters[0].required = 1;
    
    // Parameter 2: content (required)
    write_parameters[1].name = strdup("content");
    write_parameters[1].type = strdup("string");
    write_parameters[1].description = strdup("Content to write to file");
    write_parameters[1].enum_values = NULL;
    write_parameters[1].enum_count = 0;
    write_parameters[1].required = 1;
    
    // Parameter 3: create_backup (optional)
    write_parameters[2].name = strdup("create_backup");
    write_parameters[2].type = strdup("boolean");
    write_parameters[2].description = strdup("Create backup before overwriting (default: false)");
    write_parameters[2].enum_values = NULL;
    write_parameters[2].enum_count = 0;
    write_parameters[2].required = 0;
    
    // Check for allocation failures
    for (int i = 0; i < 3; i++) {
        if (write_parameters[i].name == NULL || 
            write_parameters[i].type == NULL ||
            write_parameters[i].description == NULL) {
            // Cleanup on failure
            for (int j = 0; j <= i; j++) {
                free(write_parameters[j].name);
                free(write_parameters[j].type);
                free(write_parameters[j].description);
            }
            return -1;
        }
    }
    
    // Register the tool using the new system
    result = register_tool(registry, "file_write", 
                          "Write content to file with optional backup",
                          write_parameters, 3, execute_file_write_tool_call);
    
    // Clean up temporary parameter storage
    for (int i = 0; i < 3; i++) {
        free(write_parameters[i].name);
        free(write_parameters[i].type);
        free(write_parameters[i].description);
    }
    
    if (result != 0) return -1;
    
    // 3. Register file_append tool
    ToolParameter append_parameters[2];
    
    // Parameter 1: file_path (required)
    append_parameters[0].name = strdup("file_path");
    append_parameters[0].type = strdup("string");
    append_parameters[0].description = strdup("Path to the file to append to");
    append_parameters[0].enum_values = NULL;
    append_parameters[0].enum_count = 0;
    append_parameters[0].required = 1;
    
    // Parameter 2: content (required)
    append_parameters[1].name = strdup("content");
    append_parameters[1].type = strdup("string");
    append_parameters[1].description = strdup("Content to append to file");
    append_parameters[1].enum_values = NULL;
    append_parameters[1].enum_count = 0;
    append_parameters[1].required = 1;
    
    // Check for allocation failures
    for (int i = 0; i < 2; i++) {
        if (append_parameters[i].name == NULL || 
            append_parameters[i].type == NULL ||
            append_parameters[i].description == NULL) {
            // Cleanup on failure
            for (int j = 0; j <= i; j++) {
                free(append_parameters[j].name);
                free(append_parameters[j].type);
                free(append_parameters[j].description);
            }
            return -1;
        }
    }
    
    // Register the tool using the new system
    result = register_tool(registry, "file_append", 
                          "Append content to existing file",
                          append_parameters, 2, execute_file_append_tool_call);
    
    // Clean up temporary parameter storage
    for (int i = 0; i < 2; i++) {
        free(append_parameters[i].name);
        free(append_parameters[i].type);
        free(append_parameters[i].description);
    }
    
    if (result != 0) return -1;
    
    // 4. Register file_list tool
    ToolParameter list_parameters[3];
    
    // Parameter 1: directory_path (required)
    list_parameters[0].name = strdup("directory_path");
    list_parameters[0].type = strdup("string");
    list_parameters[0].description = strdup("Path to directory to list");
    list_parameters[0].enum_values = NULL;
    list_parameters[0].enum_count = 0;
    list_parameters[0].required = 1;
    
    // Parameter 2: pattern (optional)
    list_parameters[1].name = strdup("pattern");
    list_parameters[1].type = strdup("string");
    list_parameters[1].description = strdup("Optional pattern to filter files");
    list_parameters[1].enum_values = NULL;
    list_parameters[1].enum_count = 0;
    list_parameters[1].required = 0;
    
    // Parameter 3: include_hidden (optional)
    list_parameters[2].name = strdup("include_hidden");
    list_parameters[2].type = strdup("boolean");
    list_parameters[2].description = strdup("Include hidden files (default: false)");
    list_parameters[2].enum_values = NULL;
    list_parameters[2].enum_count = 0;
    list_parameters[2].required = 0;
    
    // Check for allocation failures
    for (int i = 0; i < 3; i++) {
        if (list_parameters[i].name == NULL || 
            list_parameters[i].type == NULL ||
            list_parameters[i].description == NULL) {
            // Cleanup on failure
            for (int j = 0; j <= i; j++) {
                free(list_parameters[j].name);
                free(list_parameters[j].type);
                free(list_parameters[j].description);
            }
            return -1;
        }
    }
    
    // Register the tool using the new system
    result = register_tool(registry, "file_list", 
                          "List directory contents with optional filtering",
                          list_parameters, 3, execute_file_list_tool_call);
    
    // Clean up temporary parameter storage
    for (int i = 0; i < 3; i++) {
        free(list_parameters[i].name);
        free(list_parameters[i].type);
        free(list_parameters[i].description);
    }
    
    if (result != 0) return -1;
    
    // 5. Register file_search tool
    ToolParameter search_parameters[7];

    // Parameter 1: search_path (required)
    search_parameters[0].name = strdup("search_path");
    search_parameters[0].type = strdup("string");
    search_parameters[0].description = strdup("File or directory path to search");
    search_parameters[0].enum_values = NULL;
    search_parameters[0].enum_count = 0;
    search_parameters[0].required = 1;

    // Parameter 2: pattern (required)
    search_parameters[1].name = strdup("pattern");
    search_parameters[1].type = strdup("string");
    search_parameters[1].description = strdup("Text pattern to search for");
    search_parameters[1].enum_values = NULL;
    search_parameters[1].enum_count = 0;
    search_parameters[1].required = 1;

    // Parameter 3: case_sensitive (optional)
    search_parameters[2].name = strdup("case_sensitive");
    search_parameters[2].type = strdup("boolean");
    search_parameters[2].description = strdup("Case sensitive search (default: true)");
    search_parameters[2].enum_values = NULL;
    search_parameters[2].enum_count = 0;
    search_parameters[2].required = 0;

    // Parameter 4: recursive (optional)
    search_parameters[3].name = strdup("recursive");
    search_parameters[3].type = strdup("boolean");
    search_parameters[3].description = strdup("Search directories recursively (default: true). Automatically skips .git, node_modules, build, and other common non-text directories.");
    search_parameters[3].enum_values = NULL;
    search_parameters[3].enum_count = 0;
    search_parameters[3].required = 0;

    // Parameter 5: file_pattern (optional)
    search_parameters[4].name = strdup("file_pattern");
    search_parameters[4].type = strdup("string");
    search_parameters[4].description = strdup("File pattern filter with wildcards (e.g., '*.c', '*.js'). Only search files matching this pattern.");
    search_parameters[4].enum_values = NULL;
    search_parameters[4].enum_count = 0;
    search_parameters[4].required = 0;

    // Parameter 6: max_tokens (optional)
    search_parameters[5].name = strdup("max_tokens");
    search_parameters[5].type = strdup("number");
    search_parameters[5].description = strdup("Maximum tokens for search results (0 for no limit)");
    search_parameters[5].enum_values = NULL;
    search_parameters[5].enum_count = 0;
    search_parameters[5].required = 0;

    // Parameter 7: max_results (optional)
    search_parameters[6].name = strdup("max_results");
    search_parameters[6].type = strdup("number");
    search_parameters[6].description = strdup("Maximum number of search results (0 for no limit)");
    search_parameters[6].enum_values = NULL;
    search_parameters[6].enum_count = 0;
    search_parameters[6].required = 0;

    // Check for allocation failures
    for (int i = 0; i < 7; i++) {
        if (search_parameters[i].name == NULL ||
            search_parameters[i].type == NULL ||
            search_parameters[i].description == NULL) {
            // Cleanup on failure
            for (int j = 0; j <= i; j++) {
                free(search_parameters[j].name);
                free(search_parameters[j].type);
                free(search_parameters[j].description);
            }
            return -1;
        }
    }

    // Register the tool using the new system
    result = register_tool(registry, "file_search",
                          "Search for text patterns in files. Automatically skips binary files, large files (>1MB), and common non-text directories like .git, node_modules, build, deps.",
                          search_parameters, 7, execute_file_search_tool_call);

    // Clean up temporary parameter storage
    for (int i = 0; i < 7; i++) {
        free(search_parameters[i].name);
        free(search_parameters[i].type);
        free(search_parameters[i].description);
    }
    
    if (result != 0) return -1;
    
    // 6. Register file_info tool
    ToolParameter info_parameters[1];
    
    // Parameter 1: file_path (required)
    info_parameters[0].name = strdup("file_path");
    info_parameters[0].type = strdup("string");
    info_parameters[0].description = strdup("Path to file to get information about");
    info_parameters[0].enum_values = NULL;
    info_parameters[0].enum_count = 0;
    info_parameters[0].required = 1;
    
    // Check for allocation failures
    if (info_parameters[0].name == NULL || 
        info_parameters[0].type == NULL ||
        info_parameters[0].description == NULL) {
        free(info_parameters[0].name);
        free(info_parameters[0].type);
        free(info_parameters[0].description);
        return -1;
    }
    
    // Register the tool using the new system
    result = register_tool(registry, "file_info", 
                          "Get detailed file information and metadata",
                          info_parameters, 1, execute_file_info_tool_call);
    
    // Clean up temporary parameter storage
    free(info_parameters[0].name);
    free(info_parameters[0].type);
    free(info_parameters[0].description);
    
    if (result != 0) return -1;
    
    // 7. Register file_delta tool
    ToolParameter delta_parameters[3];
    
    // Parameter 1: file_path (required)
    delta_parameters[0].name = strdup("file_path");
    delta_parameters[0].type = strdup("string");
    delta_parameters[0].description = strdup("Path to file to modify");
    delta_parameters[0].enum_values = NULL;
    delta_parameters[0].enum_count = 0;
    delta_parameters[0].required = 1;
    
    // Parameter 2: operations (required)
    delta_parameters[1].name = strdup("operations");
    delta_parameters[1].type = strdup("array");
    delta_parameters[1].description = strdup("Array of delta operations to apply");
    delta_parameters[1].enum_values = NULL;
    delta_parameters[1].enum_count = 0;
    delta_parameters[1].required = 1;
    
    // Parameter 3: create_backup (optional)
    delta_parameters[2].name = strdup("create_backup");
    delta_parameters[2].type = strdup("boolean");
    delta_parameters[2].description = strdup("Create backup before applying changes (default: false)");
    delta_parameters[2].enum_values = NULL;
    delta_parameters[2].enum_count = 0;
    delta_parameters[2].required = 0;
    
    // Check for allocation failures
    for (int i = 0; i < 3; i++) {
        if (delta_parameters[i].name == NULL || 
            delta_parameters[i].type == NULL ||
            delta_parameters[i].description == NULL) {
            // Cleanup on failure
            for (int j = 0; j <= i; j++) {
                free(delta_parameters[j].name);
                free(delta_parameters[j].type);
                free(delta_parameters[j].description);
            }
            return -1;
        }
    }
    
    // Register the tool using the new system
    result = register_tool(registry, "file_delta", 
                          "Apply delta patch operations to file for efficient partial updates",
                          delta_parameters, 3, execute_file_delta_tool_call);
    
    // Clean up temporary parameter storage
    for (int i = 0; i < 3; i++) {
        free(delta_parameters[i].name);
        free(delta_parameters[i].type);
        free(delta_parameters[i].description);
    }
    
    return result;
}