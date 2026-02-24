#ifndef HTTP_FORM_POST_H
#define HTTP_FORM_POST_H

#include "http_client.h"

typedef struct {
    const char *key;
    const char *value;
} FormField;

int http_form_post(const char *url, const FormField *fields, int count,
                   struct HTTPResponse *response);

#endif /* HTTP_FORM_POST_H */
