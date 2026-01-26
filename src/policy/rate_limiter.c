/**
 * Rate Limiter Implementation
 *
 * Implements denial rate limiting with exponential backoff.
 * This is an opaque type that owns its own data structure.
 */

#include "rate_limiter.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* =============================================================================
 * Internal Data Structures
 * ========================================================================== */

/**
 * Entry in the rate limiter tracking table.
 */
typedef struct {
    char *key;              /* Tracked key (typically tool name) */
    int denial_count;       /* Consecutive denials */
    time_t last_denial;     /* Timestamp of most recent denial */
    time_t backoff_until;   /* Don't prompt until after this time */
} RateLimitEntry;

/**
 * Rate limiter internal structure.
 */
struct RateLimiter {
    RateLimitEntry *entries;
    int count;
    int capacity;
};

/* =============================================================================
 * Constants
 * ========================================================================== */

/* Initial capacity for entries array */
#define INITIAL_CAPACITY 8

/* Rate limiting backoff schedule (in seconds) */
static const int BACKOFF_SCHEDULE[] = {
    0,    /* 1 denial - no backoff */
    0,    /* 2 denials - no backoff */
    5,    /* 3 denials - 5 seconds */
    15,   /* 4 denials - 15 seconds */
    60,   /* 5 denials - 60 seconds */
    300   /* 6+ denials - 5 minutes */
};
#define BACKOFF_SCHEDULE_SIZE (sizeof(BACKOFF_SCHEDULE) / sizeof(BACKOFF_SCHEDULE[0]))

/* =============================================================================
 * Internal Helper Functions
 * ========================================================================== */

/**
 * Find an existing entry by key.
 * Returns NULL if not found.
 */
static RateLimitEntry *find_entry(const RateLimiter *rl, const char *key) {
    if (rl == NULL || key == NULL) {
        return NULL;
    }

    for (int i = 0; i < rl->count; i++) {
        if (rl->entries[i].key != NULL &&
            strcmp(rl->entries[i].key, key) == 0) {
            return &rl->entries[i];
        }
    }
    return NULL;
}

/**
 * Get or create an entry for a key.
 * Returns NULL on allocation failure.
 */
static RateLimitEntry *get_or_create_entry(RateLimiter *rl, const char *key) {
    if (rl == NULL || key == NULL) {
        return NULL;
    }

    /* Check if entry already exists */
    RateLimitEntry *entry = find_entry(rl, key);
    if (entry != NULL) {
        return entry;
    }

    /* Need to create a new entry */
    if (rl->count >= rl->capacity) {
        /* Grow the array */
        int new_capacity = rl->capacity * 2;
        RateLimitEntry *new_entries = realloc(rl->entries,
                                              new_capacity * sizeof(RateLimitEntry));
        if (new_entries == NULL) {
            return NULL;
        }
        rl->entries = new_entries;
        rl->capacity = new_capacity;
    }

    /* Initialize new entry */
    entry = &rl->entries[rl->count++];
    memset(entry, 0, sizeof(*entry));
    entry->key = strdup(key);
    if (entry->key == NULL) {
        rl->count--;
        return NULL;
    }

    return entry;
}

/* =============================================================================
 * Public API
 * ========================================================================== */

RateLimiter *rate_limiter_create(void) {
    RateLimiter *rl = calloc(1, sizeof(RateLimiter));
    if (rl == NULL) {
        return NULL;
    }

    rl->entries = calloc(INITIAL_CAPACITY, sizeof(RateLimitEntry));
    if (rl->entries == NULL) {
        free(rl);
        return NULL;
    }

    rl->count = 0;
    rl->capacity = INITIAL_CAPACITY;
    return rl;
}

void rate_limiter_destroy(RateLimiter *rl) {
    if (rl == NULL) {
        return;
    }

    /* Free all entries */
    if (rl->entries != NULL) {
        for (int i = 0; i < rl->count; i++) {
            free(rl->entries[i].key);
        }
        free(rl->entries);
    }

    free(rl);
}

int rate_limiter_is_blocked(const RateLimiter *rl, const char *key) {
    if (rl == NULL || key == NULL) {
        return 0;
    }

    const RateLimitEntry *entry = find_entry(rl, key);
    if (entry == NULL) {
        return 0;
    }

    time_t now = time(NULL);
    if (entry->backoff_until > now) {
        return 1; /* Still in backoff period */
    }

    return 0;
}

void rate_limiter_record_denial(RateLimiter *rl, const char *key) {
    if (rl == NULL || key == NULL) {
        return;
    }

    RateLimitEntry *entry = get_or_create_entry(rl, key);
    if (entry == NULL) {
        return;
    }

    time_t now = time(NULL);
    entry->denial_count++;
    entry->last_denial = now;

    /* Calculate backoff based on denial count */
    int schedule_index = entry->denial_count - 1;
    if (schedule_index < 0) {
        schedule_index = 0;
    }
    if (schedule_index >= (int)BACKOFF_SCHEDULE_SIZE) {
        schedule_index = BACKOFF_SCHEDULE_SIZE - 1;
    }

    int backoff_seconds = BACKOFF_SCHEDULE[schedule_index];
    entry->backoff_until = now + backoff_seconds;
}

void rate_limiter_reset(RateLimiter *rl, const char *key) {
    if (rl == NULL || key == NULL) {
        return;
    }

    RateLimitEntry *entry = find_entry(rl, key);
    if (entry == NULL) {
        return;
    }

    entry->denial_count = 0;
    entry->last_denial = 0;
    entry->backoff_until = 0;
}

int rate_limiter_get_remaining(const RateLimiter *rl, const char *key) {
    if (rl == NULL || key == NULL) {
        return 0;
    }

    const RateLimitEntry *entry = find_entry(rl, key);
    if (entry == NULL) {
        return 0;
    }

    time_t now = time(NULL);
    if (entry->backoff_until <= now) {
        return 0;
    }

    return (int)(entry->backoff_until - now);
}
