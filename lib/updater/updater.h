#ifndef UPDATER_H
#define UPDATER_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    UPDATE_AVAILABLE,
    UP_TO_DATE,
    CHECK_FAILED
} updater_status_t;

typedef struct {
    int major, minor, patch;
    char tag[32];
    char download_url[1024];
    size_t asset_size;
    char body[4096];
} updater_release_t;

/**
 * Check GitHub for a newer release.
 * Populates release struct if an update is available.
 *
 * @param release Output struct filled on UPDATE_AVAILABLE
 * @return UPDATE_AVAILABLE, UP_TO_DATE, or CHECK_FAILED
 */
updater_status_t updater_check(updater_release_t *release);

/**
 * Download the release binary to dest_path.
 *
 * @param release Release info from updater_check()
 * @param dest_path Where to save the downloaded binary
 * @return 0 on success, -1 on failure
 */
int updater_download(const updater_release_t *release, const char *dest_path);

/**
 * Replace target_path with the downloaded binary.
 * Handles cross-device moves and sets executable permission.
 *
 * @param downloaded_path Path to the downloaded binary
 * @param target_path Path of the running executable to replace
 * @return 0 on success, -1 on failure (e.g., permission denied)
 */
int updater_apply(const char *downloaded_path, const char *target_path);

/* Internal helpers exposed for testing */
int parse_semver(const char *tag, int *major, int *minor, int *patch);
int semver_compare(int maj1, int min1, int pat1, int maj2, int min2, int pat2);

#endif /* UPDATER_H */
