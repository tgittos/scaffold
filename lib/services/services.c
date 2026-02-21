#include "services.h"
#include "../db/sqlite_dal.h"
#include <stdlib.h>
#include <string.h>

Services* services_create_default(void) {
    Services* services = malloc(sizeof(Services));
    if (services == NULL) {
        return NULL;
    }

    services->message_store = message_store_create(NULL);
    services->vector_db = vector_db_service_create();
    services->embeddings = embeddings_service_create();
    services->task_store = task_store_create(NULL);

    services->metadata_store = metadata_store_create(NULL);

    document_store_set_services(services);
    services->document_store = document_store_create(NULL);

    sqlite_dal_config_t scaffold_cfg = SQLITE_DAL_CONFIG_DEFAULT;
    scaffold_cfg.default_name = "scaffold.db";
    sqlite_dal_t *scaffold_dal = sqlite_dal_create(&scaffold_cfg);
    if (scaffold_dal != NULL) {
        services->goal_store = goal_store_create_with_dal(scaffold_dal);
        services->action_store = action_store_create_with_dal(scaffold_dal);
        sqlite_dal_destroy(scaffold_dal);
    } else {
        services->goal_store = goal_store_create(NULL);
        services->action_store = action_store_create(NULL);
    }

    services->use_singletons = false;

    return services;
}

Services* services_create_empty(void) {
    Services* services = malloc(sizeof(Services));
    if (services == NULL) {
        return NULL;
    }

    services->message_store = NULL;
    services->vector_db = NULL;
    services->embeddings = NULL;
    services->task_store = NULL;
    services->document_store = NULL;
    services->metadata_store = NULL;
    services->goal_store = NULL;
    services->action_store = NULL;
    services->use_singletons = false;

    return services;
}

void services_destroy(Services* services) {
    if (services == NULL) {
        return;
    }

    if (services->message_store != NULL) {
        message_store_destroy(services->message_store);
    }
    if (services->vector_db != NULL) {
        vector_db_service_destroy(services->vector_db);
    }
    if (services->embeddings != NULL) {
        embeddings_service_destroy(services->embeddings);
    }
    if (services->task_store != NULL) {
        task_store_destroy(services->task_store);
    }
    if (services->document_store != NULL) {
        document_store_destroy(services->document_store);
    }
    if (services->metadata_store != NULL) {
        metadata_store_destroy(services->metadata_store);
    }
    if (services->goal_store != NULL) {
        goal_store_destroy(services->goal_store);
    }
    if (services->action_store != NULL) {
        action_store_destroy(services->action_store);
    }

    free(services);
}

message_store_t* services_get_message_store(Services* services) {
    return (services != NULL) ? services->message_store : NULL;
}

vector_db_service_t* services_get_vector_db(Services* services) {
    return (services != NULL) ? services->vector_db : NULL;
}

embeddings_service_t* services_get_embeddings(Services* services) {
    return (services != NULL) ? services->embeddings : NULL;
}

task_store_t* services_get_task_store(Services* services) {
    return (services != NULL) ? services->task_store : NULL;
}

document_store_t* services_get_document_store(Services* services) {
    return (services != NULL) ? services->document_store : NULL;
}

metadata_store_t* services_get_metadata_store(Services* services) {
    return (services != NULL) ? services->metadata_store : NULL;
}

goal_store_t* services_get_goal_store(Services* services) {
    return (services != NULL) ? services->goal_store : NULL;
}

action_store_t* services_get_action_store(Services* services) {
    return (services != NULL) ? services->action_store : NULL;
}
