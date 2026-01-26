/**
 * Mock Embeddings Utilities Implementation
 *
 * Generates deterministic embeddings for testing without API calls.
 * Uses hash-based embedding with semantic grouping support.
 */

#include "mock_embeddings.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Maximum number of semantic groups
#define MAX_SEMANTIC_GROUPS 32

// Maximum number of text-to-group mappings
#define MAX_TEXT_MAPPINGS 256

// Semantic group storage
typedef struct {
    int group_id;
    float base_vector[MOCK_EMBEDDING_DIM];
    int active;
} SemanticGroup;

typedef struct {
    char* text;
    int group_id;
} TextMapping;

static SemanticGroup g_semantic_groups[MAX_SEMANTIC_GROUPS];
static TextMapping g_text_mappings[MAX_TEXT_MAPPINGS];
static int g_group_count = 0;
static int g_mapping_count = 0;

// Simple string hash function (djb2)
static unsigned long hash_string(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

// Generate a deterministic pseudo-random float from a seed
static float pseudo_random(unsigned long* seed) {
    *seed = (*seed * 1103515245 + 12345) & 0x7fffffff;
    return (float)(*seed) / (float)0x7fffffff;
}

// Normalize vector to unit length
static void normalize_vector(float* vec, size_t dim) {
    float magnitude = 0.0f;
    for (size_t i = 0; i < dim; i++) {
        magnitude += vec[i] * vec[i];
    }
    magnitude = sqrtf(magnitude);
    if (magnitude > 0.0001f) {
        for (size_t i = 0; i < dim; i++) {
            vec[i] /= magnitude;
        }
    }
}

// Generate base embedding from text hash
static void generate_hash_embedding(const char* text, float* embedding) {
    unsigned long seed = hash_string(text);

    for (size_t i = 0; i < MOCK_EMBEDDING_DIM; i++) {
        // Generate value in range [-1, 1]
        embedding[i] = (pseudo_random(&seed) * 2.0f) - 1.0f;
    }

    normalize_vector(embedding, MOCK_EMBEDDING_DIM);
}

// Find semantic group for text
static SemanticGroup* find_group_for_text(const char* text) {
    for (int i = 0; i < g_mapping_count; i++) {
        if (g_text_mappings[i].text && strstr(text, g_text_mappings[i].text) != NULL) {
            // Found a matching text pattern, find the group
            for (int j = 0; j < g_group_count; j++) {
                if (g_semantic_groups[j].active &&
                    g_semantic_groups[j].group_id == g_text_mappings[i].group_id) {
                    return &g_semantic_groups[j];
                }
            }
        }
    }
    return NULL;
}

// Generate a base vector for a semantic group
static void generate_group_base_vector(int group_id, float* vec) {
    unsigned long seed = (unsigned long)group_id * 12345;

    for (size_t i = 0; i < MOCK_EMBEDDING_DIM; i++) {
        vec[i] = (pseudo_random(&seed) * 2.0f) - 1.0f;
    }

    normalize_vector(vec, MOCK_EMBEDDING_DIM);
}

// Register a semantic group internally
static int register_semantic_group(int group_id) {
    if (g_group_count >= MAX_SEMANTIC_GROUPS) {
        return -1;
    }

    // Check if already registered
    for (int i = 0; i < g_group_count; i++) {
        if (g_semantic_groups[i].group_id == group_id) {
            return 0;  // Already exists
        }
    }

    SemanticGroup* group = &g_semantic_groups[g_group_count++];
    group->group_id = group_id;
    group->active = 1;
    generate_group_base_vector(group_id, group->base_vector);

    return 0;
}

int mock_embeddings_get_vector(const char* text, float* embedding) {
    if (text == NULL || embedding == NULL) {
        return -1;
    }

    // Check if text belongs to a semantic group
    SemanticGroup* group = find_group_for_text(text);

    if (group != NULL) {
        // Start with group base vector
        memcpy(embedding, group->base_vector, MOCK_EMBEDDING_DIM * sizeof(float));

        // Add small perturbation based on text hash for uniqueness
        // Keep noise small (1% of magnitude) to maintain high similarity within groups
        unsigned long seed = hash_string(text);
        for (size_t i = 0; i < MOCK_EMBEDDING_DIM; i++) {
            // Add noise in range [-0.01, 0.01]
            float noise = (pseudo_random(&seed) * 0.02f) - 0.01f;
            embedding[i] += noise;
        }

        normalize_vector(embedding, MOCK_EMBEDDING_DIM);
    } else {
        // No semantic group, use pure hash-based embedding
        generate_hash_embedding(text, embedding);
    }

    return 0;
}

int mock_embeddings_assign_to_group(const char* text_pattern, int group_id) {
    if (g_mapping_count >= MAX_TEXT_MAPPINGS || text_pattern == NULL) {
        return -1;
    }

    // Ensure group is registered
    register_semantic_group(group_id);

    TextMapping* mapping = &g_text_mappings[g_mapping_count++];
    mapping->text = strdup(text_pattern);
    if (mapping->text == NULL) {
        g_mapping_count--;
        return -1;
    }
    mapping->group_id = group_id;

    return 0;
}

void mock_embeddings_cleanup(void) {
    for (int i = 0; i < g_mapping_count; i++) {
        free(g_text_mappings[i].text);
        g_text_mappings[i].text = NULL;
    }
    g_mapping_count = 0;
    g_group_count = 0;
    memset(g_semantic_groups, 0, sizeof(g_semantic_groups));
}

void mock_embeddings_init_test_groups(void) {
    mock_embeddings_cleanup();

    // Quantum physics group
    mock_embeddings_assign_to_group("quantum", MOCK_GROUP_QUANTUM);
    mock_embeddings_assign_to_group("Quantum", MOCK_GROUP_QUANTUM);
    mock_embeddings_assign_to_group("atomic", MOCK_GROUP_QUANTUM);
    mock_embeddings_assign_to_group("physics", MOCK_GROUP_QUANTUM);

    // Classical mechanics group
    mock_embeddings_assign_to_group("classical", MOCK_GROUP_CLASSICAL);
    mock_embeddings_assign_to_group("Classical", MOCK_GROUP_CLASSICAL);
    mock_embeddings_assign_to_group("macroscopic", MOCK_GROUP_CLASSICAL);
    mock_embeddings_assign_to_group("mechanics", MOCK_GROUP_CLASSICAL);

    // Machine learning group
    mock_embeddings_assign_to_group("machine learning", MOCK_GROUP_ML);
    mock_embeddings_assign_to_group("Machine Learning", MOCK_GROUP_ML);
    mock_embeddings_assign_to_group("artificial intelligence", MOCK_GROUP_ML);
    mock_embeddings_assign_to_group("AI", MOCK_GROUP_ML);
    mock_embeddings_assign_to_group("neural", MOCK_GROUP_ML);

    // Greeting group
    mock_embeddings_assign_to_group("Hello", MOCK_GROUP_GREETING);
    mock_embeddings_assign_to_group("hello", MOCK_GROUP_GREETING);
    mock_embeddings_assign_to_group("Hi", MOCK_GROUP_GREETING);
    mock_embeddings_assign_to_group("greeting", MOCK_GROUP_GREETING);

    // General conversation group
    mock_embeddings_assign_to_group("First", MOCK_GROUP_GENERAL);
    mock_embeddings_assign_to_group("Second", MOCK_GROUP_GENERAL);
    mock_embeddings_assign_to_group("message", MOCK_GROUP_GENERAL);
    mock_embeddings_assign_to_group("response", MOCK_GROUP_GENERAL);
}
