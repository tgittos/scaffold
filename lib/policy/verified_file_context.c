#include "verified_file_context.h"
#include "atomic_file.h"
#include "path_normalize.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

/* Thread-local storage for the current verified file context.
 * On platforms without _Thread_local, falls back to static storage
 * (not thread-safe, but ralph is currently single-threaded). */
#ifdef __STDC_NO_THREADS__
static ApprovedPath *current_context = NULL;
#else
static _Thread_local ApprovedPath *current_context = NULL;
#endif

int verified_file_context_set(const ApprovedPath *approved) {
    if (approved == NULL) {
        return -1;
    }

    verified_file_context_clear();

    current_context = malloc(sizeof(ApprovedPath));
    if (current_context == NULL) {
        return -1;
    }

    init_approved_path(current_context);

    if (approved->user_path != NULL) {
        current_context->user_path = strdup(approved->user_path);
        if (current_context->user_path == NULL) {
            goto cleanup_error;
        }
    }

    if (approved->resolved_path != NULL) {
        current_context->resolved_path = strdup(approved->resolved_path);
        if (current_context->resolved_path == NULL) {
            goto cleanup_error;
        }
    }

    if (approved->parent_path != NULL) {
        current_context->parent_path = strdup(approved->parent_path);
        if (current_context->parent_path == NULL) {
            goto cleanup_error;
        }
    }

    current_context->inode = approved->inode;
    current_context->device = approved->device;
    current_context->parent_inode = approved->parent_inode;
    current_context->parent_device = approved->parent_device;
    current_context->existed = approved->existed;
    current_context->is_network_fs = approved->is_network_fs;

#ifdef _WIN32
    current_context->volume_serial = approved->volume_serial;
    current_context->index_high = approved->index_high;
    current_context->index_low = approved->index_low;
    current_context->parent_volume_serial = approved->parent_volume_serial;
    current_context->parent_index_high = approved->parent_index_high;
    current_context->parent_index_low = approved->parent_index_low;
#endif

    return 0;

cleanup_error:
    free_approved_path(current_context);
    free(current_context);
    current_context = NULL;
    return -1;
}

void verified_file_context_clear(void) {
    if (current_context != NULL) {
        free_approved_path(current_context);
        free(current_context);
        current_context = NULL;
    }
}

int verified_file_context_is_set(void) {
    return current_context != NULL;
}

static int mode_to_flags(VerifiedFileMode mode) {
    switch (mode) {
        case VERIFIED_MODE_READ:
            return O_RDONLY;
        case VERIFIED_MODE_WRITE:
            return O_WRONLY | O_CREAT | O_TRUNC;
        case VERIFIED_MODE_APPEND:
            return O_WRONLY | O_CREAT | O_APPEND;
        case VERIFIED_MODE_READWRITE:
            return O_RDWR;
        default:
            return O_RDONLY;
    }
}

VerifyResult verified_file_context_get_fd(const char *requested_path,
                                           VerifiedFileMode mode,
                                           int *out_fd) {
    if (out_fd == NULL) {
        return VERIFY_ERR_INVALID_PATH;
    }
    *out_fd = -1;

    if (requested_path == NULL) {
        return VERIFY_ERR_INVALID_PATH;
    }

    if (current_context == NULL) {
        /* No verified context - fall back to regular open for backward compatibility.
         * This allows tools to work even without approval gates enabled. */
        int flags = mode_to_flags(mode);
        int fd;
        if (mode == VERIFIED_MODE_WRITE || mode == VERIFIED_MODE_APPEND) {
            fd = open(requested_path, flags, 0644);
        } else {
            fd = open(requested_path, flags);
        }
        if (fd < 0) {
            return VERIFY_ERR_OPEN;
        }
        *out_fd = fd;
        return VERIFY_OK;
    }

    if (!verified_file_context_path_matches(requested_path)) {
        return VERIFY_ERR_INODE_MISMATCH;
    }

    int flags = mode_to_flags(mode);
    VerifyResult result = verify_and_open_approved_path(current_context, flags, out_fd);

    return result;
}

const char *verified_file_context_get_resolved_path(void) {
    if (current_context == NULL) {
        return NULL;
    }
    return current_context->resolved_path;
}

int verified_file_context_path_matches(const char *requested_path) {
    if (requested_path == NULL || current_context == NULL) {
        return 0;
    }

    if (current_context->user_path != NULL &&
        strcmp(requested_path, current_context->user_path) == 0) {
        return 1;
    }

    if (current_context->resolved_path != NULL &&
        strcmp(requested_path, current_context->resolved_path) == 0) {
        return 1;
    }

    char *resolved_requested = atomic_file_resolve_path(requested_path, 0);
    if (resolved_requested == NULL) {
        return 0;
    }

    int matches = 0;
    if (current_context->resolved_path != NULL) {
        matches = (strcmp(resolved_requested, current_context->resolved_path) == 0);
    }

    free(resolved_requested);
    return matches;
}
