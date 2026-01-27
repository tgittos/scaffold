#include "memory_commands.h"
#include "../db/vector_db_service.h"
#include "../db/metadata_store.h"
#include "../llm/embeddings_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>

static void print_help(void) {
    printf("\n📚 \033[1mMemory Management Commands\033[0m\n");
    printf("────────────────────────────\n");
    printf("\033[1m/memory list [index]\033[0m - List all chunks (optionally from specific index)\n");
    printf("\033[1m/memory search <query>\033[0m - Search chunks by content or metadata\n");
    printf("\033[1m/memory show <chunk_id>\033[0m - Show details of a specific chunk\n");
    printf("\033[1m/memory edit <chunk_id> <field> <value>\033[0m - Edit chunk metadata\n");
    printf("\033[1m/memory indices\033[0m - List all available indices\n");
    printf("\033[1m/memory stats [index]\033[0m - Show statistics for an index\n");
    printf("\033[1m/memory help\033[0m - Show this help message\n\n");
}

static char* format_timestamp(time_t timestamp) {
    struct tm* tm_info = localtime(&timestamp);
    if (tm_info == NULL) return NULL;

    char* buffer = malloc(64);
    if (buffer == NULL) return NULL;

    strftime(buffer, 64, "%Y-%m-%d %H:%M:%S", tm_info);
    return buffer;
}

static void print_chunk_summary(const ChunkMetadata* chunk) {
    if (chunk == NULL) return;
    
    char* timestamp = format_timestamp(chunk->timestamp);
    
    printf("📄 \033[1mChunk #%zu\033[0m", chunk->chunk_id);
    if (chunk->type) printf(" [\033[36m%s\033[0m]", chunk->type);
    if (chunk->importance && strcmp(chunk->importance, "normal") != 0) {
        if (strcmp(chunk->importance, "high") == 0 || strcmp(chunk->importance, "critical") == 0) {
            printf(" \033[31m⚠️  %s\033[0m", chunk->importance);
        } else {
            printf(" [%s]", chunk->importance);
        }
    }
    printf("\n");
    
    if (timestamp) {
        printf("   📅 %s", timestamp);
        free(timestamp);
    }
    if (chunk->source) printf(" | 📍 %s", chunk->source);
    printf("\n");
    
    if (chunk->content) {
        size_t len = strlen(chunk->content);
        if (len > 100) {
            printf("   %.100s...\n", chunk->content);
        } else {
            printf("   %s\n", chunk->content);
        }
    }
    printf("\n");
}

static void print_chunk_details(const ChunkMetadata* chunk) {
    if (chunk == NULL) return;
    
    char* timestamp = format_timestamp(chunk->timestamp);
    
    printf("\n═══════════════════════════════════════\n");
    printf("📄 \033[1mChunk Details\033[0m\n");
    printf("═══════════════════════════════════════\n");
    printf("\033[1mID:\033[0m          %zu\n", chunk->chunk_id);
    printf("\033[1mIndex:\033[0m       %s\n", chunk->index_name ? chunk->index_name : "unknown");
    printf("\033[1mType:\033[0m        %s\n", chunk->type ? chunk->type : "general");
    printf("\033[1mSource:\033[0m      %s\n", chunk->source ? chunk->source : "unknown");
    printf("\033[1mImportance:\033[0m  %s\n", chunk->importance ? chunk->importance : "normal");
    
    if (timestamp) {
        printf("\033[1mTimestamp:\033[0m   %s\n", timestamp);
        free(timestamp);
    }
    
    if (chunk->custom_metadata) {
        printf("\033[1mMetadata:\033[0m    %s\n", chunk->custom_metadata);
    }
    
    printf("\n\033[1mContent:\033[0m\n");
    printf("───────────────────────────────────────\n");
    if (chunk->content) {
        printf("%s\n", chunk->content);
    } else {
        printf("(no content)\n");
    }
    printf("═══════════════════════════════════════\n\n");
}

static int cmd_list(const char* args) {
    const char* index_name = "long_term_memory";

    if (args && strlen(args) > 0) {
        index_name = args;
    }

    metadata_store_t* store = metadata_store_get_instance();
    if (store == NULL) {
        printf("❌ Failed to access metadata store\n");
        return -1;
    }

    size_t count = 0;
    ChunkMetadata** chunks = metadata_store_list(store, index_name, &count);

    if (chunks == NULL || count == 0) {
        printf("📭 No memories found in index '%s'\n", index_name);
        return 0;
    }

    printf("\n📚 \033[1mMemories in '%s' (%zu total)\033[0m\n", index_name, count);
    printf("════════════════════════════════════════\n\n");

    for (size_t i = 0; i < count; i++) {
        print_chunk_summary(chunks[i]);
    }

    metadata_store_free_chunks(chunks, count);
    return 0;
}

static int cmd_search(const char* args) {
    if (args == NULL || strlen(args) == 0) {
        printf("❌ Please provide a search query\n");
        printf("Usage: /memory search <query>\n");
        return -1;
    }

    metadata_store_t* store = metadata_store_get_instance();
    if (store == NULL) {
        printf("❌ Failed to access metadata store\n");
        return -1;
    }

    const char* index_name = "long_term_memory";
    size_t count = 0;
    ChunkMetadata** chunks = metadata_store_search(store, index_name, args, &count);
    
    if (chunks == NULL || count == 0) {
        printf("🔍 No memories found matching '%s'\n", args);
        return 0;
    }
    
    printf("\n🔍 \033[1mSearch Results for '%s' (%zu matches)\033[0m\n", args, count);
    printf("════════════════════════════════════════\n\n");
    
    for (size_t i = 0; i < count; i++) {
        print_chunk_summary(chunks[i]);
    }
    
    metadata_store_free_chunks(chunks, count);
    return 0;
}

static int cmd_show(const char* args) {
    if (args == NULL || strlen(args) == 0) {
        printf("❌ Please provide a chunk ID\n");
        printf("Usage: /memory show <chunk_id>\n");
        return -1;
    }

    char* endptr = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(args, &endptr, 10);
    if (errno == ERANGE || endptr == args || (endptr != NULL && *endptr != '\0' && !isspace(*endptr))) {
        printf("❌ Invalid chunk ID: %s\n", args);
        return -1;
    }
    size_t chunk_id = (size_t)parsed;

    metadata_store_t* store = metadata_store_get_instance();
    if (store == NULL) {
        printf("❌ Failed to access metadata store\n");
        return -1;
    }
    
    //Fall through known indices since chunk IDs aren't globally unique
    const char* index_name = "long_term_memory";
    ChunkMetadata* chunk = metadata_store_get(store, index_name, chunk_id);

    if (chunk == NULL) {
        index_name = "conversation_history";
        chunk = metadata_store_get(store, index_name, chunk_id);
    }

    if (chunk == NULL) {
        printf("❌ Chunk #%zu not found\n", chunk_id);
        return -1;
    }

    print_chunk_details(chunk);
    metadata_store_free_chunk(chunk);
    return 0;
}


static int cmd_edit(const char* args) {
    if (args == NULL || strlen(args) == 0) {
        printf("❌ Invalid syntax\n");
        printf("Usage: /memory edit <chunk_id> <field> <value>\n");
        printf("Fields: type, source, importance, content\n");
        return -1;
    }
    
    char* args_copy = strdup(args);
    if (args_copy == NULL) return -1;
    
    char* chunk_id_str = strtok(args_copy, " ");
    char* field = strtok(NULL, " ");
    char* value = strtok(NULL, "");
    
    if (chunk_id_str == NULL || field == NULL || value == NULL) {
        printf("❌ Invalid syntax\n");
        printf("Usage: /memory edit <chunk_id> <field> <value>\n");
        free(args_copy);
        return -1;
    }
    
    while (*value && isspace(*value)) value++;

    char* endptr = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(chunk_id_str, &endptr, 10);
    if (errno == ERANGE || endptr == chunk_id_str || (endptr != NULL && *endptr != '\0')) {
        printf("❌ Invalid chunk ID: %s\n", chunk_id_str);
        free(args_copy);
        return -1;
    }
    size_t chunk_id = (size_t)parsed;

    metadata_store_t* store = metadata_store_get_instance();
    if (store == NULL) {
        printf("❌ Failed to access metadata store\n");
        free(args_copy);
        return -1;
    }
    
    // Fall through known indices since chunk IDs aren't globally unique
    const char* index_name = "long_term_memory";
    ChunkMetadata* chunk = metadata_store_get(store, index_name, chunk_id);

    if (chunk == NULL) {
        index_name = "conversation_history";
        chunk = metadata_store_get(store, index_name, chunk_id);
    }
    
    if (chunk == NULL) {
        printf("❌ Chunk #%zu not found\n", chunk_id);
        free(args_copy);
        return -1;
    }
    
    bool valid_field = true;
    char* new_value = NULL;
    if (strcmp(field, "type") == 0) {
        new_value = strdup(value);
        if (new_value == NULL) {
            printf("❌ Memory allocation failed\n");
            metadata_store_free_chunk(chunk);
            free(args_copy);
            return -1;
        }
        free(chunk->type);
        chunk->type = new_value;
    } else if (strcmp(field, "source") == 0) {
        new_value = strdup(value);
        if (new_value == NULL) {
            printf("❌ Memory allocation failed\n");
            metadata_store_free_chunk(chunk);
            free(args_copy);
            return -1;
        }
        free(chunk->source);
        chunk->source = new_value;
    } else if (strcmp(field, "importance") == 0) {
        new_value = strdup(value);
        if (new_value == NULL) {
            printf("❌ Memory allocation failed\n");
            metadata_store_free_chunk(chunk);
            free(args_copy);
            return -1;
        }
        free(chunk->importance);
        chunk->importance = new_value;
    } else if (strcmp(field, "content") == 0) {
        new_value = strdup(value);
        if (new_value == NULL) {
            printf("❌ Memory allocation failed\n");
            metadata_store_free_chunk(chunk);
            free(args_copy);
            return -1;
        }
        free(chunk->content);
        chunk->content = new_value;

        // Content changes require re-embedding to keep vector search consistent
        if (embeddings_service_is_configured()) {
            vector_t* new_vector = embeddings_service_text_to_vector(value);
            if (new_vector == NULL) {
                printf("⚠️  Warning: Failed to create embedding for updated content\n");
            } else {
                vector_db_t* db = vector_db_service_get_database();
                if (db == NULL || vector_db_update_vector(db, index_name, new_vector, chunk_id) != 0) {
                    printf("⚠️  Warning: Failed to update vector embedding\n");
                }
                embeddings_service_free_vector(new_vector);
            }
        }
    } else {
        valid_field = false;
        printf("❌ Invalid field '%s'\n", field);
        printf("Valid fields: type, source, importance, content\n");
    }
    
    if (valid_field) {
        if (metadata_store_update(store, chunk) != 0) {
            printf("❌ Failed to update metadata\n");
        } else {
            printf("✅ Successfully updated chunk #%zu\n", chunk_id);
            printf("   %s = %s\n", field, value);
        }
    }
    
    metadata_store_free_chunk(chunk);
    free(args_copy);
    return valid_field ? 0 : -1;
}

static int cmd_indices(const char* args) {
    (void)args; // Unused
    
    vector_db_t* db = vector_db_service_get_database();
    if (db == NULL) {
        printf("❌ Failed to access vector database\n");
        return -1;
    }
    
    size_t count = 0;
    char** indices = vector_db_list_indices(db, &count);
    
    if (indices == NULL || count == 0) {
        printf("📭 No indices found\n");
        return 0;
    }
    
    printf("\n📚 \033[1mAvailable Indices (%zu total)\033[0m\n", count);
    printf("════════════════════════════════════════\n");
    
    for (size_t i = 0; i < count; i++) {
        size_t size = vector_db_get_index_size(db, indices[i]);
        size_t capacity = vector_db_get_index_capacity(db, indices[i]);
        
        printf("📁 \033[1m%s\033[0m\n", indices[i]);
        printf("   Vectors: %zu / %zu", size, capacity);
        
        if (capacity > 0) {
            float usage = (float)size / capacity * 100;
            printf(" (%.1f%% used)", usage);
        }
        printf("\n\n");
        
        free(indices[i]);
    }
    free(indices);
    
    return 0;
}

static int cmd_stats(const char* args) {
    const char* index_name = "long_term_memory";

    if (args && strlen(args) > 0) {
        index_name = args;
    }
    
    vector_db_t* db = vector_db_service_get_database();
    if (db == NULL) {
        printf("❌ Failed to access vector database\n");
        return -1;
    }
    
    if (!vector_db_has_index(db, index_name)) {
        printf("❌ Index '%s' not found\n", index_name);
        return -1;
    }
    
    size_t size = vector_db_get_index_size(db, index_name);
    size_t capacity = vector_db_get_index_capacity(db, index_name);
    
    metadata_store_t* store = metadata_store_get_instance();
    size_t metadata_count = 0;
    if (store != NULL) {
        ChunkMetadata** chunks = metadata_store_list(store, index_name, &metadata_count);
        if (chunks != NULL) {
            metadata_store_free_chunks(chunks, metadata_count);
        }
    }
    
    printf("\n📊 \033[1mStatistics for '%s'\033[0m\n", index_name);
    printf("════════════════════════════════════════\n");
    printf("📈 Vectors:      %zu / %zu", size, capacity);
    if (capacity > 0) {
        float usage = (float)size / capacity * 100;
        printf(" (%.1f%% used)", usage);
    }
    printf("\n");
    printf("📄 Metadata:     %zu chunks\n", metadata_count);
    
    if (size != metadata_count) {
        printf("⚠️  Warning:     Vector count doesn't match metadata count\n");
    }
    
    printf("\n");
    return 0;
}

int process_memory_command(const char* command) {
    if (command == NULL) return -1;
    
    if (strncmp(command, "/memory", 7) != 0) {
        return -1;
    }

    const char* args = command + 7;
    while (*args && isspace(*args)) args++;

    if (strlen(args) == 0 || strcmp(args, "help") == 0) {
        print_help();
        return 0;
    }

    char subcommand[32] = {0};
    const char* subargs = NULL;

    const char* space = strchr(args, ' ');
    if (space != NULL) {
        size_t len = space - args;
        if (len >= sizeof(subcommand)) len = sizeof(subcommand) - 1;
        strncpy(subcommand, args, len);
        subargs = space + 1;
        while (*subargs && isspace(*subargs)) subargs++;
    } else {
        strncpy(subcommand, args, sizeof(subcommand) - 1);
    }

    if (strcmp(subcommand, "list") == 0) {
        return cmd_list(subargs);
    } else if (strcmp(subcommand, "search") == 0) {
        return cmd_search(subargs);
    } else if (strcmp(subcommand, "show") == 0) {
        return cmd_show(subargs);
    } else if (strcmp(subcommand, "edit") == 0) {
        return cmd_edit(subargs);
    } else if (strcmp(subcommand, "indices") == 0) {
        return cmd_indices(subargs);
    } else if (strcmp(subcommand, "stats") == 0) {
        return cmd_stats(subargs);
    } else {
        printf("❌ Unknown subcommand: %s\n", subcommand);
        print_help();
        return -1;
    }
}

void memory_commands_init(void) {
    // Side effect: initializes the singleton metadata store if not yet created
    metadata_store_get_instance();
}

void memory_commands_cleanup(void) {
    // Cleanup will be handled by the singleton destructor
}