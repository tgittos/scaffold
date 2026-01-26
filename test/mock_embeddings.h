/**
 * Mock Embeddings Utilities for Testing
 *
 * Provides deterministic embedding generation for tests.
 * Use with mock_api_server to avoid real API calls.
 *
 * Usage:
 *   1. Call mock_embeddings_init_test_groups() in setUp()
 *   2. Use mock_embeddings_get_vector() to get embeddings for mock server responses
 *   3. Call mock_embeddings_cleanup() in tearDown()
 */

#ifndef MOCK_EMBEDDINGS_H
#define MOCK_EMBEDDINGS_H

#include <stddef.h>

// Mock embedding dimension (matches text-embedding-3-small)
#define MOCK_EMBEDDING_DIM 1536

/**
 * Get deterministic mock embedding for text.
 * Texts assigned to the same semantic group will have similar embeddings.
 *
 * @param text Input text
 * @param embedding Output buffer (must be at least MOCK_EMBEDDING_DIM floats)
 * @return 0 on success, -1 on failure
 */
int mock_embeddings_get_vector(const char* text, float* embedding);

/**
 * Pre-defined semantic groups for common test scenarios
 */
#define MOCK_GROUP_QUANTUM      1   // Quantum physics related
#define MOCK_GROUP_CLASSICAL    2   // Classical mechanics related
#define MOCK_GROUP_ML           3   // Machine learning related
#define MOCK_GROUP_GREETING     4   // Greetings and responses
#define MOCK_GROUP_GENERAL      5   // General conversation

/**
 * Initialize pre-defined semantic groups for testing.
 * Sets up groups for quantum, classical, ML, greetings, and general topics.
 * Call in setUp().
 */
void mock_embeddings_init_test_groups(void);

/**
 * Assign text pattern to a semantic group.
 * Texts containing this pattern will get embeddings similar to others in the group.
 *
 * @param text_pattern Text pattern to match (substring match)
 * @param group_id Semantic group ID
 * @return 0 on success, -1 on failure
 */
int mock_embeddings_assign_to_group(const char* text_pattern, int group_id);

/**
 * Clean up mock embeddings state.
 * Call in tearDown().
 */
void mock_embeddings_cleanup(void);

#endif // MOCK_EMBEDDINGS_H
