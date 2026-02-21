#include "executable_path.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EXE_PATH_BUFFER_SIZE 4096

char* get_executable_path(void) {
    char *path = malloc(EXE_PATH_BUFFER_SIZE);
    if (path == NULL) {
        return NULL;
    }

    ssize_t len = readlink("/proc/self/exe", path, EXE_PATH_BUFFER_SIZE - 1);
    if (len > 0) {
        path[len] = '\0';
        if (strstr(path, ".ape-") == NULL) {
            return path;
        }
    }

    if (getcwd(path, EXE_PATH_BUFFER_SIZE) != NULL) {
        size_t cwd_len = strlen(path);
        if (cwd_len + 10 < EXE_PATH_BUFFER_SIZE) {
            strcat(path, "/scaffold");
            if (access(path, X_OK) == 0) {
                return path;
            }
        }
    }

    free(path);
    return strdup("./scaffold");
}
