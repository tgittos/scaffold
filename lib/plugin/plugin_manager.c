#include "plugin_manager.h"
#include "../util/app_home.h"
#include "../util/debug_output.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

char *plugin_manager_get_plugins_dir(void) {
    return app_home_path("plugins");
}

void plugin_manager_init(PluginManager *mgr) {
    if (!mgr) return;
    memset(mgr, 0, sizeof(PluginManager));
}

int plugin_manager_discover(PluginManager *mgr) {
    if (!mgr) return -1;

    char *plugins_dir = plugin_manager_get_plugins_dir();
    if (!plugins_dir) {
        debug_printf("Plugin: failed to resolve plugins directory\n");
        return 0;
    }

    DIR *dir = opendir(plugins_dir);
    if (!dir) {
        debug_printf("Plugin: no plugins directory at %s\n", plugins_dir);
        free(plugins_dir);
        return 0;
    }

    int discovered = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && mgr->count < MAX_PLUGINS) {
        if (entry->d_name[0] == '.') continue;

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", plugins_dir, entry->d_name);

        struct stat lst;
        if (lstat(path, &lst) != 0) continue;
        if (S_ISLNK(lst.st_mode)) {
            debug_printf("Plugin: skipping symlink %s\n", path);
            continue;
        }
        if (!S_ISREG(lst.st_mode)) continue;
        if (!(lst.st_mode & S_IXUSR)) continue;

        mgr->plugins[mgr->count].path = strdup(path);
        if (!mgr->plugins[mgr->count].path) continue;

        mgr->plugins[mgr->count].pid = 0;
        mgr->plugins[mgr->count].stdin_fd = -1;
        mgr->plugins[mgr->count].stdout_fd = -1;
        mgr->plugins[mgr->count].initialized = 0;
        mgr->plugins[mgr->count].request_id = 1;
        mgr->count++;
        discovered++;

        debug_printf("Plugin: discovered %s\n", path);
    }

    closedir(dir);
    free(plugins_dir);

    debug_printf("Plugin: discovered %d plugin(s)\n", discovered);
    return discovered;
}

static int spawn_plugin(PluginProcess *plugin) {
    if (!plugin || !plugin->path) return -1;

    int stdin_pipe[2], stdout_pipe[2];

    if (pipe2(stdin_pipe, O_CLOEXEC) == -1) {
        debug_printf("Plugin: failed to create stdin pipe: %s\n", strerror(errno));
        return -1;
    }

    if (pipe2(stdout_pipe, O_CLOEXEC) == -1) {
        debug_printf("Plugin: failed to create stdout pipe: %s\n", strerror(errno));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        debug_printf("Plugin: fork failed: %s\n", strerror(errno));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child process */
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        /* Close inherited FDs (SQLite connections, pipes, etc.) */
        long max_fd = sysconf(_SC_OPEN_MAX);
        if (max_fd < 0) max_fd = 1024;
        for (int fd = STDERR_FILENO + 1; fd < (int)max_fd; fd++) {
            close(fd);
        }

        char *argv[] = { plugin->path, NULL };
        execv(plugin->path, argv);
        _exit(1);
    }

    /* Parent */
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    plugin->pid = pid;
    plugin->stdin_fd = stdin_pipe[1];
    plugin->stdout_fd = stdout_pipe[0];

    /* Non-blocking reads */
    int flags = fcntl(plugin->stdout_fd, F_GETFL, 0);
    fcntl(plugin->stdout_fd, F_SETFL, flags | O_NONBLOCK);

    debug_printf("Plugin: spawned %s (pid %d)\n", plugin->path, pid);
    return 0;
}

int plugin_manager_send_request(PluginProcess *plugin, const char *json, char **response) {
    if (!plugin || !json || !response) return -1;
    if (plugin->stdin_fd < 0 || plugin->stdout_fd < 0) return -1;
    if (!plugin_check_alive(plugin)) return -1;

    *response = NULL;

    /* Write full message + newline, handling partial writes */
    size_t json_len = strlen(json);
    size_t total = 0;
    while (total < json_len) {
        ssize_t w = write(plugin->stdin_fd, json + total, json_len - total);
        if (w < 0) {
            if (errno == EINTR) continue;
            debug_printf("Plugin %s: write failed: %s\n", plugin->path, strerror(errno));
            return -1;
        }
        total += w;
    }

    ssize_t nw;
    do {
        nw = write(plugin->stdin_fd, "\n", 1);
    } while (nw < 0 && errno == EINTR);
    if (nw != 1) {
        debug_printf("Plugin %s: newline write failed\n", plugin->path);
        return -1;
    }

    /* Read response with timeout */
    fd_set read_fds;
    struct timeval timeout;
    int max_retries = PLUGIN_TIMEOUT_MS / 100;

    size_t buf_size = 8192;
    size_t buf_used = 0;
    char *buffer = malloc(buf_size);
    if (!buffer) return -1;

    for (int i = 0; i < max_retries; i++) {
        FD_ZERO(&read_fds);
        FD_SET(plugin->stdout_fd, &read_fds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        int sel = select(plugin->stdout_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (sel < 0) {
            if (errno == EINTR) continue;
            debug_printf("Plugin %s: select failed: %s\n", plugin->path, strerror(errno));
            free(buffer);
            return -1;
        }

        if (sel == 0) continue;

        if (buf_used + 4096 >= buf_size) {
            if (buf_size * 2 > PLUGIN_MAX_RESPONSE_BYTES) {
                debug_printf("Plugin %s: response exceeds %d byte limit\n",
                             plugin->path, PLUGIN_MAX_RESPONSE_BYTES);
                free(buffer);
                return -1;
            }
            buf_size *= 2;
            char *nb = realloc(buffer, buf_size);
            if (!nb) { free(buffer); return -1; }
            buffer = nb;
        }

        ssize_t nr = read(plugin->stdout_fd, buffer + buf_used, buf_size - buf_used - 1);
        if (nr > 0) {
            buf_used += nr;
            if (buf_used > PLUGIN_MAX_RESPONSE_BYTES) {
                debug_printf("Plugin %s: response exceeds %d byte limit\n",
                             plugin->path, PLUGIN_MAX_RESPONSE_BYTES);
                free(buffer);
                return -1;
            }
            if (buf_used > 0 && buffer[buf_used - 1] == '\n') break;
            continue;
        }

        if (nr < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            debug_printf("Plugin %s: read error: %s\n", plugin->path, strerror(errno));
            free(buffer);
            return -1;
        }

        /* EOF */
        if (buf_used > 0) break;
        debug_printf("Plugin %s: unexpected EOF\n", plugin->path);
        free(buffer);
        return -1;
    }

    if (buf_used == 0) {
        debug_printf("Plugin %s: timeout waiting for response\n", plugin->path);
        free(buffer);
        return -1;
    }

    buffer[buf_used] = '\0';
    *response = buffer;
    return 0;
}

int plugin_validate_name(const char *name) {
    if (!name || name[0] == '\0') return -1;
    size_t len = strlen(name);
    if (len > 64) return -1;
    for (size_t i = 0; i < len; i++) {
        if (name[i] == '_' || name[i] == '/' || name[i] == '\\') return -1;
    }
    return 0;
}

int plugin_check_alive(PluginProcess *plugin) {
    if (!plugin || !plugin->initialized || plugin->pid <= 0) return 0;

    int status;
    pid_t ret = waitpid(plugin->pid, &status, WNOHANG);
    if (ret > 0 || ret < 0) {
        debug_printf("Plugin %s: process %d exited\n",
                     plugin->manifest.name ? plugin->manifest.name : "?",
                     plugin->pid);
        plugin->pid = 0;
        plugin->initialized = 0;
        if (plugin->stdin_fd >= 0) { close(plugin->stdin_fd); plugin->stdin_fd = -1; }
        if (plugin->stdout_fd >= 0) { close(plugin->stdout_fd); plugin->stdout_fd = -1; }
        return 0;
    }
    return 1;
}

static int handshake_plugin(PluginProcess *plugin) {
    char *init_msg = plugin_protocol_build_initialize(PLUGIN_PROTOCOL_VERSION);
    if (!init_msg) return -1;

    /* Set request id */
    cJSON *root = cJSON_Parse(init_msg);
    if (root) {
        cJSON_ReplaceItemInObject(root, "id", cJSON_CreateNumber(plugin->request_id++));
        free(init_msg);
        init_msg = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
    }

    char *response = NULL;
    int rc = plugin_manager_send_request(plugin, init_msg, &response);
    free(init_msg);

    if (rc != 0 || !response) {
        debug_printf("Plugin %s: handshake failed\n", plugin->path);
        return -1;
    }

    if (plugin_protocol_parse_manifest(response, &plugin->manifest) != 0) {
        debug_printf("Plugin %s: invalid manifest\n", plugin->path);
        free(response);
        return -1;
    }

    if (plugin_validate_name(plugin->manifest.name) != 0) {
        debug_printf("Plugin %s: invalid name '%s'\n", plugin->path,
                     plugin->manifest.name ? plugin->manifest.name : "(null)");
        plugin_manifest_cleanup(&plugin->manifest);
        free(response);
        return -1;
    }

    free(response);
    plugin->initialized = 1;

    debug_printf("Plugin: initialized '%s' v%s (priority %d, %d hooks, %d tools)\n",
                 plugin->manifest.name, plugin->manifest.version,
                 plugin->manifest.priority, plugin->manifest.hook_count,
                 plugin->manifest.tool_count);
    return 0;
}

int plugin_manager_start_all(PluginManager *mgr, ToolRegistry *registry) {
    if (!mgr) return -1;

    for (int i = 0; i < mgr->count; i++) {
        PluginProcess *p = &mgr->plugins[i];

        if (spawn_plugin(p) != 0) {
            debug_printf("Plugin: failed to spawn %s\n", p->path);
            continue;
        }

        if (handshake_plugin(p) != 0) {
            debug_printf("Plugin: failed to handshake %s, killing\n", p->path);
            if (p->pid > 0) {
                kill(p->pid, SIGKILL);
                waitpid(p->pid, NULL, 0);
                p->pid = 0;
            }
            if (p->stdin_fd >= 0) { close(p->stdin_fd); p->stdin_fd = -1; }
            if (p->stdout_fd >= 0) { close(p->stdout_fd); p->stdout_fd = -1; }
            continue;
        }

        /* Reject duplicate plugin names */
        int duplicate = 0;
        for (int j = 0; j < i; j++) {
            if (!mgr->plugins[j].initialized) continue;
            if (strcmp(mgr->plugins[j].manifest.name, p->manifest.name) == 0) {
                fprintf(stderr, "Warning: duplicate plugin name '%s' from %s "
                                "(already loaded from %s), shutting down duplicate\n",
                        p->manifest.name, p->path, mgr->plugins[j].path);
                duplicate = 1;
                break;
            }
        }
        if (duplicate) {
            if (p->pid > 0) {
                kill(p->pid, SIGKILL);
                waitpid(p->pid, NULL, 0);
                p->pid = 0;
            }
            if (p->stdin_fd >= 0) { close(p->stdin_fd); p->stdin_fd = -1; }
            if (p->stdout_fd >= 0) { close(p->stdout_fd); p->stdout_fd = -1; }
            plugin_manifest_cleanup(&p->manifest);
            p->initialized = 0;
            continue;
        }

        /* Register plugin-provided tools */
        if (registry && p->manifest.tool_count > 0) {
            for (int j = 0; j < p->manifest.tool_count; j++) {
                ToolFunction *src = &p->manifest.tools[j];

                char prefixed[256];
                snprintf(prefixed, sizeof(prefixed), "plugin_%s_%s",
                         p->manifest.name, src->name);

                ToolFunction reg = {0};
                reg.name = strdup(prefixed);
                reg.description = src->description ? strdup(src->description) : NULL;
                reg.execute_func = NULL;
                reg.thread_safe = 0;
                reg.parameter_count = src->parameter_count;

                if (src->parameter_count > 0 && src->parameters) {
                    reg.parameters = calloc(src->parameter_count, sizeof(ToolParameter));
                    if (reg.parameters) {
                        for (int k = 0; k < src->parameter_count; k++) {
                            reg.parameters[k].name = src->parameters[k].name ? strdup(src->parameters[k].name) : NULL;
                            reg.parameters[k].type = src->parameters[k].type ? strdup(src->parameters[k].type) : NULL;
                            reg.parameters[k].description = src->parameters[k].description ? strdup(src->parameters[k].description) : NULL;
                            reg.parameters[k].required = src->parameters[k].required;
                            reg.parameters[k].enum_values = NULL;
                            reg.parameters[k].enum_count = 0;
                            reg.parameters[k].items_schema = NULL;
                        }
                    }
                }

                if (ToolFunctionArray_push(&registry->functions, reg) != 0) {
                    debug_printf("Plugin: failed to register tool %s\n", prefixed);
                    free(reg.name);
                    free(reg.description);
                    for (int k = 0; k < reg.parameter_count; k++) {
                        free(reg.parameters[k].name);
                        free(reg.parameters[k].type);
                        free(reg.parameters[k].description);
                    }
                    free(reg.parameters);
                } else {
                    debug_printf("Plugin: registered tool %s\n", prefixed);
                }
            }
        }
    }

    return 0;
}

static void shutdown_plugin(PluginProcess *plugin) {
    if (!plugin) return;

    if (plugin->stdin_fd >= 0 && plugin->initialized) {
        char *shutdown_msg = plugin_protocol_build_shutdown();
        if (shutdown_msg) {
            cJSON *root = cJSON_Parse(shutdown_msg);
            if (root) {
                cJSON_ReplaceItemInObject(root, "id", cJSON_CreateNumber(plugin->request_id++));
                free(shutdown_msg);
                shutdown_msg = cJSON_PrintUnformatted(root);
                cJSON_Delete(root);
            }

            char *response = NULL;
            plugin_manager_send_request(plugin, shutdown_msg, &response);
            free(shutdown_msg);
            free(response);
        }
    }

    if (plugin->stdin_fd >= 0) {
        close(plugin->stdin_fd);
        plugin->stdin_fd = -1;
    }
    if (plugin->stdout_fd >= 0) {
        close(plugin->stdout_fd);
        plugin->stdout_fd = -1;
    }

    if (plugin->pid > 0) {
        kill(plugin->pid, SIGTERM);

        int status;
        int waited = 0;
        for (int i = 0; i < 10; i++) {
            if (waitpid(plugin->pid, &status, WNOHANG) > 0) {
                waited = 1;
                break;
            }
            usleep(100000);
        }

        if (!waited) {
            kill(plugin->pid, SIGKILL);
            waitpid(plugin->pid, &status, 0);
        }
        plugin->pid = 0;
    }

    plugin_manifest_cleanup(&plugin->manifest);
    free(plugin->path);
    plugin->path = NULL;
    plugin->initialized = 0;
}

void plugin_manager_shutdown_all(PluginManager *mgr) {
    if (!mgr) return;

    for (int i = 0; i < mgr->count; i++) {
        shutdown_plugin(&mgr->plugins[i]);
    }
    mgr->count = 0;
}

int plugin_manager_execute_tool(PluginManager *mgr, const ToolCall *tool_call, ToolResult *result) {
    if (!mgr || !tool_call || !result) return -1;

    /* Tool name format: plugin_<pluginname>_<toolname> */
    if (strncmp(tool_call->name, "plugin_", 7) != 0) return -1;

    const char *rest = tool_call->name + 7;
    /* Safe: plugin_validate_name() rejects underscores in plugin names,
     * so the first '_' in rest is always the separator. */
    const char *sep = strchr(rest, '_');
    if (!sep) return -1;

    size_t plugin_name_len = sep - rest;
    const char *tool_name = sep + 1;

    PluginProcess *target = NULL;
    for (int i = 0; i < mgr->count; i++) {
        if (!mgr->plugins[i].initialized) continue;
        if (strlen(mgr->plugins[i].manifest.name) == plugin_name_len &&
            strncmp(mgr->plugins[i].manifest.name, rest, plugin_name_len) == 0) {
            target = &mgr->plugins[i];
            break;
        }
    }

    if (!target) {
        result->tool_call_id = tool_call->id ? strdup(tool_call->id) : NULL;
        result->result = strdup("Plugin not found");
        result->success = 0;
        return -1;
    }

    char *request = plugin_protocol_build_tool_execute(tool_name, tool_call->arguments);
    if (!request) return -1;

    cJSON *root = cJSON_Parse(request);
    if (root) {
        cJSON_ReplaceItemInObject(root, "id", cJSON_CreateNumber(target->request_id++));
        free(request);
        request = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
    }

    char *response = NULL;
    int rc = plugin_manager_send_request(target, request, &response);
    free(request);

    if (rc != 0 || !response) {
        result->tool_call_id = tool_call->id ? strdup(tool_call->id) : NULL;
        result->result = strdup("Plugin communication failed");
        result->success = 0;
        return -1;
    }

    PluginToolResult ptr;
    if (plugin_protocol_parse_tool_result(response, &ptr) != 0) {
        free(response);
        result->tool_call_id = tool_call->id ? strdup(tool_call->id) : NULL;
        result->result = strdup("Failed to parse plugin tool result");
        result->success = 0;
        return -1;
    }
    free(response);

    result->tool_call_id = tool_call->id ? strdup(tool_call->id) : NULL;
    result->result = ptr.result ? ptr.result : strdup("");
    result->success = ptr.success;
    /* Don't call plugin_tool_result_cleanup since we transferred ownership of result */
    return 0;
}

