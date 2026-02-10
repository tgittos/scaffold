#include "image_attachment.h"
#include <mbedtls/base64.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *supported_extensions[] = {
    ".png", ".jpg", ".jpeg", ".gif", ".webp", NULL
};

static int is_image_extension(const char *ext) {
    for (int i = 0; supported_extensions[i] != NULL; i++) {
        if (strcasecmp(ext, supported_extensions[i]) == 0) return 1;
    }
    return 0;
}

static const char *mime_from_extension(const char *ext) {
    if (strcasecmp(ext, ".png") == 0) return "image/png";
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, ".gif") == 0) return "image/gif";
    if (strcasecmp(ext, ".webp") == 0) return "image/webp";
    return NULL;
}

static const char *find_extension(const char *path) {
    const char *dot = strrchr(path, '.');
    if (dot == NULL || dot == path) return NULL;
    return dot;
}

static const char *find_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static char *read_file_base64(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) return NULL;

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    if (file_size <= 0 || (size_t)file_size > IMAGE_ATTACHMENT_MAX_SIZE) {
        fclose(fp);
        if (file_size > 0) {
            fprintf(stderr, "Warning: Image file '%s' too large (%ld bytes, max %d)\n",
                    path, file_size, IMAGE_ATTACHMENT_MAX_SIZE);
        }
        return NULL;
    }
    fseek(fp, 0, SEEK_SET);

    unsigned char *raw = malloc((size_t)file_size);
    if (raw == NULL) { fclose(fp); return NULL; }

    size_t read_count = fread(raw, 1, (size_t)file_size, fp);
    fclose(fp);
    if (read_count != (size_t)file_size) { free(raw); return NULL; }

    /* Get required base64 output size */
    size_t b64_len = 0;
    mbedtls_base64_encode(NULL, 0, &b64_len, raw, read_count);

    unsigned char *b64 = malloc(b64_len + 1);
    if (b64 == NULL) { free(raw); return NULL; }

    int ret = mbedtls_base64_encode(b64, b64_len + 1, &b64_len, raw, read_count);
    free(raw);
    if (ret != 0) { free(b64); return NULL; }

    b64[b64_len] = '\0';
    *out_len = b64_len;
    return (char *)b64;
}

int image_attachment_parse(const char *text, ImageParseResult *result) {
    if (text == NULL || result == NULL) return -1;

    result->items = NULL;
    result->count = 0;
    result->cleaned_text = NULL;

    /* Worst case: each @ref expands to [image: ...] placeholder (longer than original) */
    size_t text_len = strlen(text);
    size_t cleaned_cap = text_len * 2 + 256;
    char *cleaned = malloc(cleaned_cap);
    if (cleaned == NULL) return -1;

    size_t capacity = 4;
    ImageAttachment *items = malloc(capacity * sizeof(ImageAttachment));
    if (items == NULL) { free(cleaned); return -1; }

    size_t count = 0;
    size_t ci = 0; /* cleaned index */
    const char *p = text;

    while (*p != '\0') {
        if (*p != '@') {
            cleaned[ci++] = *p++;
            continue;
        }

        /* Found '@' — check if it's followed by a path with an image extension */
        const char *start = p + 1;
        if (*start == '\0' || isspace((unsigned char)*start)) {
            cleaned[ci++] = *p++;
            continue;
        }

        /* Scan to end of path (whitespace or end of string) */
        const char *end = start;
        while (*end != '\0' && !isspace((unsigned char)*end)) end++;

        size_t path_len = (size_t)(end - start);
        char *path = malloc(path_len + 1);
        if (path == NULL) { cleaned[ci++] = *p++; continue; }
        memcpy(path, start, path_len);
        path[path_len] = '\0';

        const char *ext = find_extension(path);
        if (ext == NULL || !is_image_extension(ext)) {
            /* Not an image reference — pass through unchanged */
            free(path);
            cleaned[ci++] = *p++;
            continue;
        }

        /* It's an image reference — try to load the file */
        const char *mime = mime_from_extension(ext);
        size_t b64_len = 0;
        char *b64 = read_file_base64(path, &b64_len);
        if (b64 == NULL) {
            fprintf(stderr, "Warning: Could not read image '%s', leaving @reference in text\n", path);
            free(path);
            cleaned[ci++] = *p++;
            continue;
        }

        /* Success — add attachment */
        if (count >= capacity) {
            capacity *= 2;
            ImageAttachment *tmp = realloc(items, capacity * sizeof(ImageAttachment));
            if (tmp == NULL) { free(b64); free(path); cleaned[ci++] = *p++; continue; }
            items = tmp;
        }

        const char *basename = find_basename(path);
        char *dup_filename = strdup(basename);
        char *dup_mime = strdup(mime);
        if (dup_filename == NULL || dup_mime == NULL) {
            free(dup_filename);
            free(dup_mime);
            free(b64);
            free(path);
            cleaned[ci++] = *p++;
            continue;
        }
        items[count].filename = dup_filename;
        items[count].mime_type = dup_mime;
        items[count].base64_data = b64;
        count++;

        /* Write placeholder into cleaned text */
        int written = snprintf(cleaned + ci, cleaned_cap - ci, "[image: %s]", basename);
        if (written > 0 && (size_t)written < cleaned_cap - ci) ci += (size_t)written;

        free(path);
        p = end; /* advance past the @path */
    }

    cleaned[ci] = '\0';

    /* Shrink cleaned text to actual size */
    char *final_cleaned = realloc(cleaned, ci + 1);
    if (final_cleaned != NULL) cleaned = final_cleaned;

    result->items = items;
    result->count = count;
    result->cleaned_text = cleaned;

    /* If no images found, shrink items to NULL */
    if (count == 0) {
        free(items);
        result->items = NULL;
    }

    return 0;
}

void image_attachment_cleanup(ImageParseResult *result) {
    if (result == NULL) return;
    for (size_t i = 0; i < result->count; i++) {
        free(result->items[i].filename);
        free(result->items[i].mime_type);
        free(result->items[i].base64_data);
    }
    free(result->items);
    free(result->cleaned_text);
    result->items = NULL;
    result->count = 0;
    result->cleaned_text = NULL;
}
