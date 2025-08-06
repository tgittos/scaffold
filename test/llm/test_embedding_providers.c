#include "../../src/llm/embedding_provider.h"
#include "../unity/unity.h"
#include <stdio.h>
#include <string.h>

void setUp(void) {
    // Set up run before each test
}

void tearDown(void) {
    // Clean up run after each test
}

void test_embedding_provider_registry_init(void) {
    EmbeddingProviderRegistry registry;
    int result = init_embedding_provider_registry(&registry);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(0, registry.count);
    TEST_ASSERT_EQUAL(0, registry.capacity);
    TEST_ASSERT_NULL(registry.providers);
    
    cleanup_embedding_provider_registry(&registry);
}

void test_register_embedding_providers(void) {
    EmbeddingProviderRegistry registry;
    init_embedding_provider_registry(&registry);
    
    // Register OpenAI provider
    int result = register_openai_embedding_provider(&registry);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, registry.count);
    
    // Register local provider
    result = register_local_embedding_provider(&registry);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(2, registry.count);
    
    cleanup_embedding_provider_registry(&registry);
}

void test_detect_openai_provider(void) {
    EmbeddingProviderRegistry registry;
    init_embedding_provider_registry(&registry);
    register_openai_embedding_provider(&registry);
    register_local_embedding_provider(&registry);
    
    // Test OpenAI API URL detection
    EmbeddingProvider *provider = detect_embedding_provider_for_url(&registry, "https://api.openai.com/v1/embeddings");
    TEST_ASSERT_NOT_NULL(provider);
    TEST_ASSERT_EQUAL_STRING("OpenAI Embeddings", provider->capabilities.name);
    
    // Test Azure OpenAI detection
    provider = detect_embedding_provider_for_url(&registry, "https://openai.azure.com/openai/deployments/test/embeddings");
    TEST_ASSERT_NOT_NULL(provider);
    TEST_ASSERT_EQUAL_STRING("OpenAI Embeddings", provider->capabilities.name);
    
    cleanup_embedding_provider_registry(&registry);
}

void test_detect_local_provider(void) {
    EmbeddingProviderRegistry registry;
    init_embedding_provider_registry(&registry);
    register_openai_embedding_provider(&registry);
    register_local_embedding_provider(&registry);
    
    // Test LMStudio URL detection
    EmbeddingProvider *provider = detect_embedding_provider_for_url(&registry, "http://localhost:1234/v1/embeddings");
    TEST_ASSERT_NOT_NULL(provider);
    TEST_ASSERT_EQUAL_STRING("Local Embeddings", provider->capabilities.name);
    
    // Test another local URL
    provider = detect_embedding_provider_for_url(&registry, "http://127.0.0.1:8080/v1/embeddings");
    TEST_ASSERT_NOT_NULL(provider);
    TEST_ASSERT_EQUAL_STRING("Local Embeddings", provider->capabilities.name);
    
    cleanup_embedding_provider_registry(&registry);
}

void test_provider_capabilities(void) {
    EmbeddingProviderRegistry registry;
    init_embedding_provider_registry(&registry);
    register_openai_embedding_provider(&registry);
    register_local_embedding_provider(&registry);
    
    // Test OpenAI provider capabilities
    EmbeddingProvider *openai_provider = detect_embedding_provider_for_url(&registry, "https://api.openai.com/v1/embeddings");
    TEST_ASSERT_NOT_NULL(openai_provider);
    TEST_ASSERT_EQUAL(1, openai_provider->capabilities.requires_auth);
    TEST_ASSERT_EQUAL_STRING("text-embedding-3-small", openai_provider->capabilities.default_model);
    TEST_ASSERT_EQUAL_STRING("Authorization: Bearer %s", openai_provider->capabilities.auth_header_format);
    
    // Test local provider capabilities
    EmbeddingProvider *local_provider = detect_embedding_provider_for_url(&registry, "http://localhost:1234/v1/embeddings");
    TEST_ASSERT_NOT_NULL(local_provider);
    TEST_ASSERT_EQUAL(0, local_provider->capabilities.requires_auth);
    TEST_ASSERT_EQUAL_STRING("Qwen3-Embedding-0.6B-Q8_0.gguf", local_provider->capabilities.default_model);
    
    cleanup_embedding_provider_registry(&registry);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_embedding_provider_registry_init);
    RUN_TEST(test_register_embedding_providers);
    RUN_TEST(test_detect_openai_provider);
    RUN_TEST(test_detect_local_provider);
    RUN_TEST(test_provider_capabilities);
    
    return UNITY_END();
}