#ifndef JWT_DECODE_H
#define JWT_DECODE_H

#include <stddef.h>

/*
 * Extract a nested claim from a JWT payload without signature verification.
 * Token is transport-authenticated via TLS, so we only need to decode the payload.
 *
 * For OpenAI tokens, the structure is:
 *   {"https://api.openai.com/auth": {"chatgpt_account_id": "..."}}
 *
 * @param jwt         Full JWT string (header.payload.signature)
 * @param parent_key  Top-level key in the payload JSON
 * @param child_key   Key within the parent object
 * @param out         Output buffer for the claim value
 * @param out_len     Size of output buffer
 * @return 0 on success, -1 on error (malformed JWT, missing claim, buffer too small)
 */
int jwt_extract_nested_claim(const char *jwt, const char *parent_key,
                              const char *child_key, char *out, size_t out_len);

#endif /* JWT_DECODE_H */
