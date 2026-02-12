/**
 * scaffold - AI Agent Scaffolding CLI
 *
 * Thin wrapper around libagent that parses command-line arguments
 * and invokes the agent API with app_name="scaffold".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../lib/agent/agent.h"
#include "../../lib/util/app_home.h"
#include "../../lib/util/config.h"
#include "build/version.h"

#define MAX_CLI_ALLOW_ENTRIES 64
#define MAX_CLI_ALLOW_CATEGORIES 16

static void print_version(void) {
    printf("scaffold %s (%s)\n", RALPH_VERSION, RALPH_GIT_HASH);
}

static void print_help(const char *program_name) {
    printf("scaffold %s - AI Agent Scaffolding\n\n", RALPH_VERSION);
    printf("Usage: %s [OPTIONS] [MESSAGE]\n\n", program_name);
    printf("Options:\n");
    printf("  -h, --help        Show this help message and exit\n");
    printf("  -v, --version     Show version information and exit\n");
    printf("  --debug           Enable debug output (shows HTTP requests)\n");
    printf("  --no-stream       Disable response streaming\n");
    printf("  --json            Enable JSON output mode\n");
    printf("  --home <path>     Override home directory (default: ~/.local/scaffold)\n");
    printf("  --yolo            Disable all approval gates for this session\n");
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

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            print_version();
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help(argv[0]);
            return EXIT_SUCCESS;
        }
    }

    AgentConfig config = agent_config_default();
    config.app_name = "scaffold";

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

    Agent agent;
    if (agent_init(&agent, &config) != 0) {
        fprintf(stderr, "Error: Failed to initialize scaffold agent\n");
        return EXIT_FAILURE;
    }

    if (agent_load_config(&agent) != 0) {
        fprintf(stderr, "Error: Failed to load scaffold configuration\n");
        agent_cleanup(&agent);
        return EXIT_FAILURE;
    }

    int result = agent_run(&agent);

    agent_cleanup(&agent);
    return (result == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
