/**
 * scaffold - AI Agent Orchestrator CLI
 *
 * Full agent wrapper around libagent with GOAP orchestration,
 * Python tooling, and update management. app_name="scaffold".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../../lib/agent/agent.h"
#include "../../lib/updater/updater.h"
#include "../../lib/util/executable_path.h"
#include "../../lib/util/app_home.h"
#include "../../lib/util/config.h"
#include "../../lib/auth/openai_login.h"
#include "../tools/python_extension.h"
#include "build/version.h"

#define MAX_CLI_ALLOW_ENTRIES 64
#define MAX_CLI_ALLOW_CATEGORIES 16

static void print_version(void) {
    printf("scaffold %s (%s)\n", RALPH_VERSION, RALPH_GIT_HASH);
}

static void print_help(const char *program_name) {
    printf("scaffold %s - AI Orchestrator\n\n", RALPH_VERSION);
    printf("Usage: %s [OPTIONS] [MESSAGE]\n\n", program_name);
    printf("Options:\n");
    printf("  -h, --help        Show this help message and exit\n");
    printf("  -v, --version     Show version information and exit\n");
    printf("  --debug           Enable debug output (shows HTTP requests)\n");
    printf("  --no-stream       Disable response streaming\n");
    printf("  --json            Enable JSON output mode\n");
    printf("  --home <path>     Override home directory (default: ~/.local/scaffold)\n");
    printf("  --yolo            Disable all approval gates for this session\n");
    printf("  --login           Log in to OpenAI via OAuth (ChatGPT subscription)\n");
    printf("  --logout          Log out of OpenAI OAuth session\n");
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
        printf("\nRun: scaffold --update\n");
        return EXIT_SUCCESS;
    case UP_TO_DATE:
        printf("scaffold %s is up to date.\n", RALPH_VERSION);
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
        printf("scaffold %s is already up to date.\n", RALPH_VERSION);
        return EXIT_SUCCESS;
    }
    if (status == CHECK_FAILED) {
        fprintf(stderr, "Failed to check for updates.\n");
        return EXIT_FAILURE;
    }

    printf("Downloading %s...\n", release.tag);

    char *tmp_path = app_home_path("scaffold.update.tmp");
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
        fprintf(stderr, "Error: Could not replace binary. Try: sudo scaffold --update\n");
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
    int login_flag = 0;
    int logout_flag = 0;
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
        if (strcmp(argv[i], "--login") == 0) {
            login_flag = 1;
        }
        if (strcmp(argv[i], "--logout") == 0) {
            logout_flag = 1;
        }
        if (strcmp(argv[i], "--home") == 0 && i + 1 < argc) {
            home_dir_override = argv[i + 1];
        }
    }

    if (login_flag || logout_flag) {
        app_home_set_app_name("scaffold");
        app_home_init(home_dir_override);
        app_home_ensure_exists();
        char *db = app_home_path("oauth2.db");
        int rc;
        if (logout_flag) {
            rc = openai_logout(db);
        } else {
            rc = openai_login(db);
            if (rc == 0) {
                config_init();
                config_set("api_url", "https://chatgpt.com/backend-api/codex/responses");
                char *cfg_path = app_home_path("config.json");
                if (cfg_path) {
                    config_save_to_file(cfg_path);
                    free(cfg_path);
                }
                config_cleanup();
                printf("API URL set to Codex endpoint.\n");
            }
        }
        free(db);
        app_home_cleanup();
        return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (check_update_flag || update_flag) {
        app_home_set_app_name("scaffold");
        app_home_init(home_dir_override);
        app_home_ensure_exists();
        if (update_flag) {
            int rc = handle_update();
            app_home_cleanup();
            return rc;
        }
        int rc = handle_check_update();
        app_home_cleanup();
        return rc;
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
    int supervisor_mode = 0;
    char *supervisor_goal_id = NULL;
    char *supervisor_phase_str = NULL;
    char *model_override = NULL;
    char *system_prompt_file = NULL;

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
        } else if (strcmp(argv[i], "--supervisor") == 0) {
            supervisor_mode = 1;
        } else if (strcmp(argv[i], "--goal") == 0 && i + 1 < argc) {
            supervisor_goal_id = argv[++i];
        } else if (strcmp(argv[i], "--phase") == 0 && i + 1 < argc) {
            supervisor_phase_str = argv[++i];
        } else if (strcmp(argv[i], "--system-prompt-file") == 0 && i + 1 < argc) {
            system_prompt_file = argv[++i];
        } else if (message_arg_index == -1 && argv[i][0] != '-') {
            message_arg_index = i;
        }
    }

    config.allow_entries = cli_allow_entries;
    config.allow_entry_count = cli_allow_count;
    config.allow_categories = cli_allow_categories;
    config.allow_category_count = cli_allow_category_count;

    if (supervisor_mode) {
        if (supervisor_goal_id == NULL) {
            fprintf(stderr, "Error: --supervisor requires --goal argument\n");
            return EXIT_FAILURE;
        }
        config.mode = AGENT_MODE_SUPERVISOR;
        config.supervisor_goal_id = supervisor_goal_id;
        if (supervisor_phase_str != NULL) {
            if (strcmp(supervisor_phase_str, "plan") == 0)
                config.supervisor_phase = SUPERVISOR_PHASE_PLAN;
            else if (strcmp(supervisor_phase_str, "execute") == 0)
                config.supervisor_phase = SUPERVISOR_PHASE_EXECUTE;
            else {
                fprintf(stderr, "Error: --phase must be 'plan' or 'execute'\n");
                return EXIT_FAILURE;
            }
        }
    } else if (worker_mode) {
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

    /* Read system prompt from file if provided */
    char *loaded_system_prompt = NULL;
    if (system_prompt_file != NULL) {
        FILE *fp = fopen(system_prompt_file, "r");
        if (fp == NULL) {
            fprintf(stderr, "Error: Cannot open system prompt file: %s\n",
                    system_prompt_file);
            return EXIT_FAILURE;
        }
        fseek(fp, 0, SEEK_END);
        long fsize = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (fsize > 0) {
            loaded_system_prompt = malloc(fsize + 1);
            if (loaded_system_prompt != NULL) {
                size_t nread = fread(loaded_system_prompt, 1, fsize, fp);
                loaded_system_prompt[nread] = '\0';
                config.worker_system_prompt = loaded_system_prompt;
            }
        }
        fclose(fp);
        unlink(system_prompt_file);
    }

    if (python_extension_register() != 0) {
        fprintf(stderr, "Warning: Failed to register Python extension\n");
    }

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

    if (config.mode == AGENT_MODE_INTERACTIVE &&
        config_get_bool("check_updates", true)) {
        updater_release_t release;
        if (updater_check(&release) == UPDATE_AVAILABLE) {
            fprintf(stderr, "Update available: %s (current: %s). Run: scaffold --update\n",
                    release.tag, RALPH_VERSION);
        }
    }

    int result = agent_run(&agent);

    agent_cleanup(&agent);
    openai_auth_cleanup();
    free(loaded_system_prompt);
    return (result == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
