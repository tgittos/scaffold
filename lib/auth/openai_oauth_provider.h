#ifndef OPENAI_OAUTH_PROVIDER_H
#define OPENAI_OAUTH_PROVIDER_H

#include "../db/oauth2_store.h"

/* Keep in sync with OPENAI_REDIRECT_URI below */
#define OAUTH_CALLBACK_PORT  1455

#define OPENAI_AUTH_URL      "https://auth.openai.com/oauth/authorize"
#define OPENAI_TOKEN_URL     "https://auth.openai.com/oauth/token"
#define OPENAI_CLIENT_ID     "app_EMoamEEZ73f0CkXaXp7hrann"
#define OPENAI_REDIRECT_URI  "http://localhost:1455/auth/callback"
#define OPENAI_SCOPE         "openid profile email offline_access"
#define OPENAI_PROVIDER_NAME "openai"

const OAuth2ProviderOps *openai_oauth_provider_ops(void);

#endif /* OPENAI_OAUTH_PROVIDER_H */
