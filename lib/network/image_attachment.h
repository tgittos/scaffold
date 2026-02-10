#ifndef IMAGE_ATTACHMENT_H
#define IMAGE_ATTACHMENT_H

#include <stddef.h>

#define IMAGE_ATTACHMENT_MAX_SIZE (20 * 1024 * 1024) /* 20 MB */

typedef struct {
    char *filename;     /* basename for placeholder text */
    char *mime_type;    /* "image/png", "image/jpeg", etc. */
    char *base64_data;  /* base64-encoded file contents */
} ImageAttachment;

typedef struct {
    ImageAttachment *items;
    size_t count;
    char *cleaned_text; /* user message with @refs replaced by [image: filename] */
} ImageParseResult;

int image_attachment_parse(const char *text, ImageParseResult *result);
void image_attachment_cleanup(ImageParseResult *result);

#endif /* IMAGE_ATTACHMENT_H */
