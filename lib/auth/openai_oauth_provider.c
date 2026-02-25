#include "openai_oauth_provider.h"
#include "../network/http_form_post.h"
#include <cJSON.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *openai_build_auth_url(const char *client_id, const char *redirect_uri,
                                    const char *scope, const char *state,
                                    const char *code_challenge) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    char *enc_scope = curl_easy_escape(curl, scope ? scope : OPENAI_SCOPE, 0);
    char *enc_redirect = curl_easy_escape(curl,
        redirect_uri ? redirect_uri : OPENAI_REDIRECT_URI, 0);
    char *enc_client = curl_easy_escape(curl, client_id, 0);
    char *enc_state = curl_easy_escape(curl, state, 0);
    char *enc_challenge = curl_easy_escape(curl, code_challenge, 0);

    if (!enc_scope || !enc_redirect || !enc_client || !enc_state || !enc_challenge) {
        curl_free(enc_scope);
        curl_free(enc_redirect);
        curl_free(enc_client);
        curl_free(enc_state);
        curl_free(enc_challenge);
        curl_easy_cleanup(curl);
        return NULL;
    }

    size_t len = strlen(OPENAI_AUTH_URL) + strlen(enc_client) + strlen(enc_redirect)
                 + strlen(enc_scope) + strlen(enc_state) + strlen(enc_challenge) + 256;
    char *url = malloc(len);
    if (!url) {
        curl_free(enc_scope);
        curl_free(enc_redirect);
        curl_free(enc_client);
        curl_free(enc_state);
        curl_free(enc_challenge);
        curl_easy_cleanup(curl);
        return NULL;
    }

    snprintf(url, len,
        "%s?response_type=code"
        "&client_id=%s"
        "&redirect_uri=%s"
        "&scope=%s"
        "&state=%s"
        "&code_challenge=%s"
        "&code_challenge_method=S256"
        "&id_token_add_organizations=true"
        "&codex_cli_simplified_flow=true",
        OPENAI_AUTH_URL, enc_client, enc_redirect, enc_scope,
        enc_state, enc_challenge);

    curl_free(enc_scope);
    curl_free(enc_redirect);
    curl_free(enc_client);
    curl_free(enc_state);
    curl_free(enc_challenge);
    curl_easy_cleanup(curl);
    return url;
}

static OAuth2Error parse_token_response(const char *json_data, long http_status,
                                         char *access_token, size_t at_len,
                                         char *refresh_token, size_t rt_len,
                                         int64_t *expires_in) {
    if (!json_data) return OAUTH2_ERROR_NETWORK;

    cJSON *root = cJSON_Parse(json_data);
    if (!root) {
        /* Non-JSON response: use HTTP status to distinguish provider vs network errors */
        if (http_status >= 400 && http_status < 500) return OAUTH2_ERROR_PROVIDER;
        if (http_status >= 500) return OAUTH2_ERROR_NETWORK;
        return OAUTH2_ERROR_NETWORK;
    }

    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error && cJSON_IsString(error)) {
        cJSON_Delete(root);
        return OAUTH2_ERROR_PROVIDER;
    }

    cJSON *at = cJSON_GetObjectItem(root, "access_token");
    if (!at || !cJSON_IsString(at)) {
        cJSON_Delete(root);
        return OAUTH2_ERROR_PROVIDER;
    }
    snprintf(access_token, at_len, "%s", at->valuestring);

    if (refresh_token && rt_len > 0) {
        cJSON *rt = cJSON_GetObjectItem(root, "refresh_token");
        if (rt && cJSON_IsString(rt)) {
            snprintf(refresh_token, rt_len, "%s", rt->valuestring);
        } else {
            refresh_token[0] = '\0';
        }
    }

    cJSON *exp = cJSON_GetObjectItem(root, "expires_in");
    if (exp && cJSON_IsNumber(exp)) {
        *expires_in = (int64_t)exp->valuedouble;
    } else {
        *expires_in = 3600;
    }

    cJSON_Delete(root);
    return OAUTH2_OK;
}

static OAuth2Error openai_exchange_code(const char *client_id, const char *client_secret,
                                         const char *redirect_uri, const char *code,
                                         const char *code_verifier,
                                         char *access_token, size_t at_len,
                                         char *refresh_token, size_t rt_len,
                                         int64_t *expires_in) {
    (void)client_secret;

    FormField fields[] = {
        {"grant_type",    "authorization_code"},
        {"client_id",     client_id},
        {"code",          code},
        {"code_verifier", code_verifier},
        {"redirect_uri",  redirect_uri ? redirect_uri : OPENAI_REDIRECT_URI},
    };

    struct HTTPResponse response = {0};
    int rc = http_form_post(OPENAI_TOKEN_URL, fields, 5, &response);
    if (rc != 0) {
        cleanup_response(&response);
        return OAUTH2_ERROR_NETWORK;
    }

    OAuth2Error err = parse_token_response(response.data, response.http_status,
                                            access_token, at_len,
                                            refresh_token, rt_len, expires_in);
    cleanup_response(&response);
    return err;
}

static OAuth2Error openai_refresh_token(const char *client_id, const char *client_secret,
                                         const char *refresh_token_in,
                                         char *access_token, size_t at_len,
                                         char *new_refresh_token, size_t rt_len,
                                         int64_t *expires_in) {
    (void)client_secret;

    FormField fields[] = {
        {"grant_type",    "refresh_token"},
        {"client_id",     client_id},
        {"refresh_token", refresh_token_in},
    };

    struct HTTPResponse response = {0};
    int rc = http_form_post(OPENAI_TOKEN_URL, fields, 3, &response);
    if (rc != 0) {
        cleanup_response(&response);
        return OAUTH2_ERROR_NETWORK;
    }

    OAuth2Error err = parse_token_response(response.data, response.http_status,
                                            access_token, at_len,
                                            new_refresh_token, rt_len, expires_in);
    cleanup_response(&response);
    return err;
}

static const OAuth2ProviderOps openai_ops = {
    .name           = OPENAI_PROVIDER_NAME,
    .build_auth_url = openai_build_auth_url,
    .exchange_code  = openai_exchange_code,
    .refresh_token  = openai_refresh_token,
    .revoke_token   = NULL,
};

const OAuth2ProviderOps *openai_oauth_provider_ops(void) {
    return &openai_ops;
}
