#include "updater.h"
#include "../network/http_client.h"
#include "build/version.h"
#include <cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define GITHUB_OWNER "tgittos"
#define GITHUB_REPO  "scaffold"
#define GITHUB_API_URL "https://api.github.com/repos/" GITHUB_OWNER "/" GITHUB_REPO "/releases/latest"

#define COPY_BUF_SIZE 65536

int parse_semver(const char *tag, int *major, int *minor, int *patch) {
    if (tag == NULL || major == NULL || minor == NULL || patch == NULL) {
        return -1;
    }
    const char *p = tag;
    if (*p == 'v' || *p == 'V') {
        p++;
    }
    if (sscanf(p, "%d.%d.%d", major, minor, patch) != 3) {
        return -1;
    }
    return 0;
}

int semver_compare(int maj1, int min1, int pat1, int maj2, int min2, int pat2) {
    if (maj1 != maj2) return (maj1 > maj2) ? 1 : -1;
    if (min1 != min2) return (min1 > min2) ? 1 : -1;
    if (pat1 != pat2) return (pat1 > pat2) ? 1 : -1;
    return 0;
}

updater_status_t updater_check(updater_release_t *release) {
    if (release == NULL) {
        return CHECK_FAILED;
    }
    memset(release, 0, sizeof(*release));

    char user_agent[64];
    snprintf(user_agent, sizeof(user_agent), "User-Agent: scaffold/%s", RALPH_VERSION);

    const char *headers[] = {
        user_agent,
        "Accept: application/vnd.github+json",
        NULL
    };

    struct HTTPConfig config = {
        .timeout_seconds = 3,
        .connect_timeout_seconds = 2,
        .follow_redirects = 1,
        .max_redirects = 5
    };

    struct HTTPResponse response = {0};
    int rc = http_get_with_config(GITHUB_API_URL, headers, &config, &response);
    if (rc != 0 || response.data == NULL) {
        cleanup_response(&response);
        return CHECK_FAILED;
    }

    cJSON *json = cJSON_Parse(response.data);
    cleanup_response(&response);
    if (json == NULL) {
        return CHECK_FAILED;
    }

    cJSON *tag_name = cJSON_GetObjectItem(json, "tag_name");
    if (!cJSON_IsString(tag_name)) {
        cJSON_Delete(json);
        return CHECK_FAILED;
    }

    int remote_major, remote_minor, remote_patch;
    if (parse_semver(tag_name->valuestring, &remote_major, &remote_minor, &remote_patch) != 0) {
        cJSON_Delete(json);
        return CHECK_FAILED;
    }

    int cmp = semver_compare(remote_major, remote_minor, remote_patch,
                             RALPH_VERSION_MAJOR, RALPH_VERSION_MINOR, RALPH_VERSION_PATCH);
    if (cmp <= 0) {
        cJSON_Delete(json);
        return UP_TO_DATE;
    }

    /* Find the "scaffold" asset */
    cJSON *assets = cJSON_GetObjectItem(json, "assets");
    if (!cJSON_IsArray(assets)) {
        cJSON_Delete(json);
        return CHECK_FAILED;
    }

    bool found_asset = false;
    int asset_count = cJSON_GetArraySize(assets);
    for (int i = 0; i < asset_count; i++) {
        cJSON *asset = cJSON_GetArrayItem(assets, i);
        cJSON *name = cJSON_GetObjectItem(asset, "name");
        if (cJSON_IsString(name) && strcmp(name->valuestring, "scaffold") == 0) {
            cJSON *url = cJSON_GetObjectItem(asset, "browser_download_url");
            if (cJSON_IsString(url)) {
                snprintf(release->download_url, sizeof(release->download_url),
                         "%s", url->valuestring);
            }
            cJSON *size = cJSON_GetObjectItem(asset, "size");
            if (cJSON_IsNumber(size)) {
                release->asset_size = (size_t)size->valuedouble;
            }
            found_asset = true;
            break;
        }
    }

    if (!found_asset || release->download_url[0] == '\0') {
        cJSON_Delete(json);
        return CHECK_FAILED;
    }

    release->major = remote_major;
    release->minor = remote_minor;
    release->patch = remote_patch;
    snprintf(release->tag, sizeof(release->tag), "%s", tag_name->valuestring);

    cJSON *body = cJSON_GetObjectItem(json, "body");
    if (cJSON_IsString(body)) {
        snprintf(release->body, sizeof(release->body), "%s", body->valuestring);
    }

    cJSON_Delete(json);
    return UPDATE_AVAILABLE;
}

int updater_download(const updater_release_t *release, const char *dest_path) {
    if (release == NULL || dest_path == NULL || release->download_url[0] == '\0') {
        return -1;
    }

    const char *headers[] = {
        "Accept: application/octet-stream",
        NULL
    };

    struct HTTPConfig config = {
        .timeout_seconds = 0,
        .connect_timeout_seconds = 30,
        .follow_redirects = 1,
        .max_redirects = 10
    };

    size_t bytes_written = 0;
    int rc = http_download_file(release->download_url, headers, &config,
                                dest_path, &bytes_written);
    if (rc != 0) {
        return -1;
    }

    if (release->asset_size > 0 && bytes_written != release->asset_size) {
        unlink(dest_path);
        return -1;
    }

    return 0;
}

int updater_apply(const char *downloaded_path, const char *target_path) {
    if (downloaded_path == NULL || target_path == NULL) {
        return -1;
    }

    if (chmod(downloaded_path, 0755) != 0) {
        return -1;
    }

    /* Back up the current binary so we can restore on failure */
    char backup_path[1024];
    snprintf(backup_path, sizeof(backup_path), "%s.bak", target_path);
    int has_backup = (rename(target_path, backup_path) == 0);

    if (rename(downloaded_path, target_path) == 0) {
        if (has_backup) unlink(backup_path);
        return 0;
    }

    if (errno != EXDEV) {
        if (has_backup) rename(backup_path, target_path);
        return -1;
    }

    /* Cross-device: copy then remove source */
    int src_fd = open(downloaded_path, O_RDONLY);
    if (src_fd < 0) {
        if (has_backup) rename(backup_path, target_path);
        return -1;
    }

    int dst_fd = open(target_path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (dst_fd < 0) {
        close(src_fd);
        if (has_backup) rename(backup_path, target_path);
        return -1;
    }

    char buf[COPY_BUF_SIZE];
    ssize_t nread;
    int result = 0;
    while ((nread = read(src_fd, buf, sizeof(buf))) > 0) {
        ssize_t total = 0;
        while (total < nread) {
            ssize_t nw = write(dst_fd, buf + total, (size_t)(nread - total));
            if (nw <= 0) {
                result = -1;
                break;
            }
            total += nw;
        }
        if (result != 0) break;
    }
    if (nread < 0) {
        result = -1;
    }

    close(src_fd);
    close(dst_fd);

    if (result != 0) {
        unlink(target_path);
        if (has_backup) rename(backup_path, target_path);
        return -1;
    }

    unlink(downloaded_path);
    if (has_backup) unlink(backup_path);
    return 0;
}
