#ifndef CODEX_PROVIDER_H
#define CODEX_PROVIDER_H

#define CODEX_MAX_ACCOUNT_ID_LEN 128

void codex_set_account_id(const char *account_id);
const char *codex_get_account_id(void);

#endif /* CODEX_PROVIDER_H */
