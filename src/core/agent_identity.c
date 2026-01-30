#include "agent_identity.h"
#include <stdlib.h>
#include <string.h>

AgentIdentity* agent_identity_create(const char* id, const char* parent_id) {
    AgentIdentity* identity = calloc(1, sizeof(AgentIdentity));
    if (identity == NULL) {
        return NULL;
    }

    if (pthread_mutex_init(&identity->mutex, NULL) != 0) {
        free(identity);
        return NULL;
    }

    if (id != NULL) {
        strncpy(identity->id, id, AGENT_ID_MAX_LENGTH - 1);
        identity->id[AGENT_ID_MAX_LENGTH - 1] = '\0';
    }

    if (parent_id != NULL && parent_id[0] != '\0') {
        strncpy(identity->parent_id, parent_id, AGENT_ID_MAX_LENGTH - 1);
        identity->parent_id[AGENT_ID_MAX_LENGTH - 1] = '\0';
        identity->is_subagent = true;
    } else {
        identity->parent_id[0] = '\0';
        identity->is_subagent = false;
    }

    return identity;
}

void agent_identity_destroy(AgentIdentity* identity) {
    if (identity == NULL) {
        return;
    }

    pthread_mutex_destroy(&identity->mutex);
    free(identity);
}

char* agent_identity_get_id(AgentIdentity* identity) {
    if (identity == NULL) {
        return NULL;
    }

    pthread_mutex_lock(&identity->mutex);
    char* copy = (identity->id[0] != '\0') ? strdup(identity->id) : NULL;
    pthread_mutex_unlock(&identity->mutex);

    return copy;
}

char* agent_identity_get_parent_id(AgentIdentity* identity) {
    if (identity == NULL) {
        return NULL;
    }

    pthread_mutex_lock(&identity->mutex);
    char* copy = (identity->parent_id[0] != '\0') ? strdup(identity->parent_id) : NULL;
    pthread_mutex_unlock(&identity->mutex);

    return copy;
}

bool agent_identity_is_subagent(AgentIdentity* identity) {
    if (identity == NULL) {
        return false;
    }

    pthread_mutex_lock(&identity->mutex);
    bool result = identity->is_subagent;
    pthread_mutex_unlock(&identity->mutex);

    return result;
}

int agent_identity_set_id(AgentIdentity* identity, const char* id) {
    if (identity == NULL) {
        return -1;
    }

    pthread_mutex_lock(&identity->mutex);
    if (id != NULL) {
        strncpy(identity->id, id, AGENT_ID_MAX_LENGTH - 1);
        identity->id[AGENT_ID_MAX_LENGTH - 1] = '\0';
    } else {
        identity->id[0] = '\0';
    }
    pthread_mutex_unlock(&identity->mutex);

    return 0;
}

int agent_identity_set_parent_id(AgentIdentity* identity, const char* parent_id) {
    if (identity == NULL) {
        return -1;
    }

    pthread_mutex_lock(&identity->mutex);
    if (parent_id != NULL && parent_id[0] != '\0') {
        strncpy(identity->parent_id, parent_id, AGENT_ID_MAX_LENGTH - 1);
        identity->parent_id[AGENT_ID_MAX_LENGTH - 1] = '\0';
        identity->is_subagent = true;
    } else {
        identity->parent_id[0] = '\0';
        identity->is_subagent = false;
    }
    pthread_mutex_unlock(&identity->mutex);

    return 0;
}
