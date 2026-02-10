/**
 * ralph - AI Assistant CLI
 *
 * This is a thin wrapper around libralph that parses command-line arguments
 * and invokes the agent API.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../lib/agent/agent.h"
#include "../../lib/updater/updater.h"
#include "../../lib/util/executable_path.h"
#include "../../lib/util/ralph_home.h"
#include "../../lib/util/config.h"
#include "../tools/python_extension.h"
#include "util/debug_output.h"
#include "build/version.h"

#define MAX_CLI_ALLOW_ENTRIES 64
#define MAX_CLI_ALLOW_CATEGORIES 16

static void print_version(void) {
    printf("ralph %s (%s)\n", RALPH_VERSION, RALPH_GIT_HASH);
}

static void print_help(const char *program_name) {
    printf("ralph %s - AI Assistant\n\n", RALPH_VERSION);
    printf("Usage: %s [OPTIONS] [MESSAGE]\n\n", program_name);
    printf("Options:\n");
    printf("  -h, --help        Show this help message and exit\n");
    printf("  -v, --version     Show version information and exit\n");
    printf("  --debug           Enable debug output (shows HTTP requests)\n");
    printf("  --no-stream       Disable response streaming\n");
    printf("  --json            Enable JSON output mode\n");
    printf("  --home <path>     Override Ralph home directory (default: ~/.local/ralph)\n");
    printf("  --yolo            Disable all approval gates for this session\n");
    printf("  --check-update    Check for updates and exit\n");
    printf("  --update          Download and apply the latest update, then exit\n");
    printf("\n");
    printf("Arguments:\n");
    printf("  MESSAGE           Process a single message and exit\n");
    printf("                    If omitted, enters interactive mode\n");
    printf("\n");
    printf("Interactive Mode Commands:\n");
    printf("  quit, exit        Exit the program\n");
    printf("  /memory           Memory management commands (use /memory help for details)\n");
    printf("  Ctrl+D            End session\n");
}

static int handle_check_update(void) {
    updater_release_t release;
    updater_status_t status = updater_check(&release);

    switch (status) {
    case UPDATE_AVAILABLE:
        printf("Update available: %s (current: %s)\n", release.tag, RALPH_VERSION);
        if (release.body[0] != '\0') {
            printf("\n%s\n", release.body);
        }
        printf("\nRun: ralph --update\n");
        return EXIT_SUCCESS;
    case UP_TO_DATE:
        printf("ralph %s is up to date.\n", RALPH_VERSION);
        return EXIT_SUCCESS;
    case CHECK_FAILED:
        fprintf(stderr, "Failed to check for updates.\n");
        return EXIT_FAILURE;
    }
    return EXIT_FAILURE;
}

static int handle_update(void) {
    printf("Checking for updates...\n");

    updater_release_t release;
    updater_status_t status = updater_check(&release);

    if (status == UP_TO_DATE) {
        printf("ralph %s is already up to date.\n", RALPH_VERSION);
        return EXIT_SUCCESS;
    }
    if (status == CHECK_FAILED) {
        fprintf(stderr, "Failed to check for updates.\n");
        return EXIT_FAILURE;
    }

    printf("Downloading %s...\n", release.tag);

    char *tmp_path = ralph_home_path("ralph.update.tmp");
    if (tmp_path == NULL) {
        fprintf(stderr, "Error: Could not resolve download path.\n");
        return EXIT_FAILURE;
    }

    if (updater_download(&release, tmp_path) != 0) {
        fprintf(stderr, "Error: Download failed.\n");
        free(tmp_path);
        return EXIT_FAILURE;
    }

    char *exe_path = get_executable_path();
    if (exe_path == NULL) {
        fprintf(stderr, "Error: Could not determine executable path.\n");
        unlink(tmp_path);
        free(tmp_path);
        return EXIT_FAILURE;
    }

    printf("Applying update to %s...\n", exe_path);

    if (updater_apply(tmp_path, exe_path) != 0) {
        fprintf(stderr, "Error: Could not replace binary. Try: sudo ralph --update\n");
        unlink(tmp_path);
        free(tmp_path);
        free(exe_path);
        return EXIT_FAILURE;
    }

    printf("Updated to %s successfully.\n", release.tag);
    free(tmp_path);
    free(exe_path);
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
    int check_update_flag = 0;
    int update_flag = 0;
    const char *home_dir_override = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            print_version();
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help(argv[0]);
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "--check-update") == 0) {
            check_update_flag = 1;
        }
        if (strcmp(argv[i], "--update") == 0) {
            update_flag = 1;
        }
        if (strcmp(argv[i], "--home") == 0 && i + 1 < argc) {
            home_dir_override = argv[i + 1];
        }
    }

    if (check_update_flag || update_flag) {
        ralph_home_init(home_dir_override);
        ralph_home_ensure_exists();
        if (update_flag) {
            int rc = handle_update();
            ralph_home_cleanup();
            return rc;
        }
        int rc = handle_check_update();
        ralph_home_cleanup();
        return rc;
    }

    AgentConfig config = agent_config_default();

    const char *cli_allow_entries[MAX_CLI_ALLOW_ENTRIES];
    int cli_allow_count = 0;
    const char *cli_allow_categories[MAX_CLI_ALLOW_CATEGORIES];
    int cli_allow_category_count = 0;

    int message_arg_index = -1;
    int subagent_mode = 0;
    char *subagent_task = NULL;
    char *subagent_context = NULL;
    int worker_mode = 0;
    char *worker_queue = NULL;
    char *model_override = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            config.debug = true;
        } else if (strcmp(argv[i], "--no-stream") == 0) {
            config.no_stream = true;
        } else if (strcmp(argv[i], "--json") == 0) {
            config.json_mode = true;
        } else if (strcmp(argv[i], "--yolo") == 0) {
            config.yolo = true;
        } else if (strcmp(argv[i], "--no-auto-messages") == 0) {
            config.no_auto_messages = true;
        } else if (strcmp(argv[i], "--message-poll-interval") == 0 && i + 1 < argc) {
            config.message_poll_interval_ms = atoi(argv[++i]);
            if (config.message_poll_interval_ms < 100) {
                config.message_poll_interval_ms = 100;
            }
        } else if (strcmp(argv[i], "--home") == 0 && i + 1 < argc) {
            config.home_dir = argv[++i];
        } else if (strcmp(argv[i], "--allow") == 0 && i + 1 < argc) {
            if (cli_allow_count < MAX_CLI_ALLOW_ENTRIES) {
                cli_allow_entries[cli_allow_count++] = argv[++i];
            } else {
                fprintf(stderr, "Warning: Too many --allow entries (max %d)\n",
                        MAX_CLI_ALLOW_ENTRIES);
                i++;
            }
        } else if (strncmp(argv[i], "--allow-category=", 17) == 0) {
            if (cli_allow_category_count < MAX_CLI_ALLOW_CATEGORIES) {
                cli_allow_categories[cli_allow_category_count++] = argv[i] + 17;
            } else {
                fprintf(stderr, "Warning: Too many --allow-category entries (max %d)\n",
                        MAX_CLI_ALLOW_CATEGORIES);
            }
        } else if (strcmp(argv[i], "--subagent") == 0) {
            subagent_mode = 1;
        } else if (strcmp(argv[i], "--task") == 0 && i + 1 < argc) {
            subagent_task = argv[++i];
        } else if (strcmp(argv[i], "--context") == 0 && i + 1 < argc) {
            subagent_context = argv[++i];
        } else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            model_override = argv[++i];
        } else if (strcmp(argv[i], "--worker") == 0) {
            worker_mode = 1;
        } else if (strcmp(argv[i], "--queue") == 0 && i + 1 < argc) {
            worker_queue = argv[++i];
        } else if (message_arg_index == -1 && argv[i][0] != '-') {
            message_arg_index = i;
        }
    }

    config.allow_entries = cli_allow_entries;
    config.allow_entry_count = cli_allow_count;
    config.allow_categories = cli_allow_categories;
    config.allow_category_count = cli_allow_category_count;

    if (worker_mode) {
        if (worker_queue == NULL) {
            fprintf(stderr, "Error: --worker requires --queue argument\n");
            return EXIT_FAILURE;
        }
        config.mode = AGENT_MODE_WORKER;
        config.worker_queue_name = worker_queue;
    } else if (subagent_mode) {
        if (subagent_task == NULL) {
            fprintf(stderr, "Error: --subagent requires --task argument\n");
            return EXIT_FAILURE;
        }
        config.mode = AGENT_MODE_BACKGROUND;
        config.subagent_task = subagent_task;
        config.subagent_context = subagent_context;
    } else if (message_arg_index != -1) {
        config.mode = AGENT_MODE_SINGLE_SHOT;
        config.initial_message = argv[message_arg_index];
    } else {
        config.mode = AGENT_MODE_INTERACTIVE;
    }

    config.model_override = model_override;

    if (python_extension_register() != 0) {
        fprintf(stderr, "Warning: Failed to register Python extension\n");
    }

    Agent agent;
    if (agent_init(&agent, &config) != 0) {
        fprintf(stderr, "Error: Failed to initialize Ralph agent\n");
        return EXIT_FAILURE;
    }

    if (agent_load_config(&agent) != 0) {
        fprintf(stderr, "Error: Failed to load Ralph configuration\n");
        agent_cleanup(&agent);
        return EXIT_FAILURE;
    }

    if (config.mode == AGENT_MODE_INTERACTIVE &&
        config_get_bool("check_updates", true)) {
        updater_release_t release;
        if (updater_check(&release) == UPDATE_AVAILABLE) {
            fprintf(stderr, "Update available: %s (current: %s). Run: ralph --update\n",
                    release.tag, RALPH_VERSION);
        }
    }

    int result = agent_run(&agent);

    agent_cleanup(&agent);
    return (result == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
