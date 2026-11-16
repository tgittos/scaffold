#include "shell_rules.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==========================================================================
 * Tables
 * ========================================================================== */

/* How a global option consumes the token after it. */
typedef enum {
    OPT_FLAG,    /* takes no value: --no-pager */
    OPT_VALUE,   /* takes a value: -C <dir>, --git-dir=<dir> */
    OPT_NUMBER   /* takes an optional numeric value: make -j [N] */
} GlobalOptKind;

typedef struct {
    const char   *name;
    GlobalOptKind kind;
} GlobalOpt;

typedef struct {
    const char      *command;
    const GlobalOpt *options;      /* name == NULL terminated */
    int              assignments;  /* 1 = takes VAR=value arguments (make); they
                                    * make the command unsaveable */
} SubcommandTool;

static const GlobalOpt GIT_OPTS[] = {
    {"-C", OPT_VALUE}, {"--git-dir", OPT_VALUE}, {"--work-tree", OPT_VALUE},
    {"--no-pager", OPT_FLAG},
    /* Known, but always-ask: they can run arbitrary code. Listed so the
     * subcommand still resolves and the flag check names the real offender. */
    {"-c", OPT_VALUE}, {"--exec-path", OPT_FLAG},
    {NULL, OPT_FLAG}
};

static const GlobalOpt DOCKER_OPTS[] = {
    {"--context", OPT_VALUE}, {"-H", OPT_VALUE}, {NULL, OPT_FLAG}
};

static const GlobalOpt KUBECTL_OPTS[] = {
    {"-n", OPT_VALUE}, {"--namespace", OPT_VALUE}, {"--context", OPT_VALUE},
    {NULL, OPT_FLAG}
};

static const GlobalOpt MAKE_OPTS[] = {
    {"-C", OPT_VALUE}, {"-j", OPT_NUMBER},
    /* Known, but always-ask: they choose the makefile or inject rules. */
    {"-f", OPT_VALUE}, {"--file", OPT_VALUE},
    {"-E", OPT_VALUE}, {"--eval", OPT_VALUE},
    {NULL, OPT_FLAG}
};

static const GlobalOpt NO_OPTS[] = {
    {NULL, OPT_FLAG}
};

static const SubcommandTool SUBCOMMAND_TOOLS[] = {
    {"git", GIT_OPTS, 0},
    {"docker", DOCKER_OPTS, 0},
    {"kubectl", KUBECTL_OPTS, 0},
    {"make", MAKE_OPTS, 1},
    {"npm", NO_OPTS, 0},
    {"pnpm", NO_OPTS, 0},
    {"yarn", NO_OPTS, 0},
    {"cargo", NO_OPTS, 0},
    {"go", NO_OPTS, 0},
    {"brew", NO_OPTS, 0},
    {"gh", NO_OPTS, 0},
    {"uv", NO_OPTS, 0},
    {"poetry", NO_OPTS, 0},
    {"pipx", NO_OPTS, 0},
    {"bundle", NO_OPTS, 0},
    {NULL, NULL, 0}
};

/*
 * Flags that change what a command does. A segment carrying one never creates
 * a rule and never matches one. anywhere marks the flags that still count after
 * "--", because the program acts on them wherever they appear (find actions,
 * sed -i, curl -o); the rest are operands once "--" has been seen.
 */
typedef struct {
    const char *command;     /* NULL = any command */
    const char *subcommand;  /* NULL = any subcommand */
    const char *flag;
    int         prefix;      /* 1 = match any flag starting with this */
    int         anywhere;
} AlwaysAskFlag;

static const AlwaysAskFlag ALWAYS_ASK_FLAGS[] = {
    {"git", "push", "--force", 0, 0},
    {"git", "push", "--force-with-lease", 1, 0},
    {"git", "push", "-f", 0, 0},
    {"git", "reset", "--hard", 0, 0},
    {"git", "clean", "-f", 0, 0},
    {"git", "clean", "--force", 0, 0},
    {"git", "push", "--delete", 0, 0},
    {"git", "push", "-d", 0, 0},
    {"git", "push", "--mirror", 0, 0},
    {"git", "push", "--prune", 0, 0},
    /* git grep's pager flag runs its value through the shell. */
    {"git", "grep", "-O", 1, 1},
    {"git", "grep", "--open-files-in-pager", 1, 1},
    {"git", "checkout", "--force", 0, 0},
    {"git", "checkout", "-f", 0, 0},
    {"git", "checkout", "--", 0, 0},
    {"git", "branch", "-D", 0, 0},
    {"git", "branch", "-d", 0, 0},
    {"git", "branch", "--delete", 0, 0},
    {"git", "branch", "-m", 0, 0},
    {"git", "branch", "-M", 0, 0},
    {"git", "branch", "--move", 0, 0},
    {"git", "branch", "-c", 0, 0},
    {"git", "branch", "-C", 0, 0},
    {"git", "tag", "-d", 0, 0},
    {"git", "tag", "--delete", 0, 0},
    {"git", "tag", "-f", 0, 0},
    {"git", "tag", "--force", 0, 0},
    /* git -c and --exec-path can run arbitrary code through config and aliases. */
    {"git", NULL, "-c", 0, 1},
    {"git", NULL, "--exec-path", 1, 1},
    {"sed", NULL, "-i", 1, 1},
    /* make picks up rules from a chosen makefile or straight off the command line. */
    {"make", NULL, "-f", 0, 0},
    {"make", NULL, "--file", 1, 0},
    {"make", NULL, "-E", 0, 0},
    {"make", NULL, "--eval", 1, 0},
    /* rm's recursive and forced removals. */
    {"rm", NULL, "-r", 0, 0},
    {"rm", NULL, "-R", 0, 0},
    {"rm", NULL, "--recursive", 0, 0},
    {"rm", NULL, "-f", 0, 0},
    {"rm", NULL, "--force", 0, 0},
    /* curl writes files, uploads them and reads more options from a config. */
    {"curl", NULL, "-o", 0, 1},
    {"curl", NULL, "-O", 0, 1},
    {"curl", NULL, "--output", 1, 1},
    {"curl", NULL, "--remote-name", 1, 1},
    {"curl", NULL, "-T", 0, 1},
    {"curl", NULL, "--upload-file", 1, 1},
    {"curl", NULL, "-K", 0, 1},
    {"curl", NULL, "--config", 1, 1},
    {"curl", NULL, "--next", 0, 1},
    {"curl", NULL, "-d", 0, 1},
    {"curl", NULL, "--data", 1, 1},
    {"curl", NULL, "-F", 0, 1},
    {"curl", NULL, "--form", 1, 1},
    /* wget writes files and posts them. */
    {"wget", NULL, "-O", 0, 1},
    {"wget", NULL, "--output-document", 1, 1},
    {"wget", NULL, "-P", 0, 1},
    {"wget", NULL, "--post-file", 1, 1},
    {"wget", NULL, "--post-data", 1, 1},
    /* tar's filter and checkpoint options run a program. */
    {"tar", NULL, "--to-command", 1, 1},
    {"tar", NULL, "--use-compress-program", 1, 1},
    {"tar", NULL, "-I", 0, 1},
    {"tar", NULL, "--checkpoint-action", 1, 1},
    {"tar", NULL, "--info-script", 1, 1},
    {"tar", NULL, "-F", 0, 1},
    {"tar", NULL, "--new-volume-script", 1, 1},
    /* find's actions run programs or write files; they are checked on every
     * command. */
    {NULL, NULL, "-exec", 1, 1},
    {NULL, NULL, "-ok", 1, 1},
    {NULL, NULL, "-delete", 1, 1},
    {NULL, NULL, "-fprint", 1, 1},
    {NULL, NULL, "-fls", 1, 1},
    {NULL, NULL, "--output", 1, 1},
    {NULL, NULL, NULL, 0, 0}
};

/* Commands whose rule must stay narrow: the rule is the first two tokens and
 * matches by prefix, so "a" on "python foo.py" never covers "python bar.py". */
static const char *NEVER_BROADEN[] = {
    "python", "python3", "node", "ruby", "perl", "bash", "sh", "zsh",
    "curl", "wget", "ssh", "scp", "rm",
    /* These run a script given as their argument. */
    "sed", "awk", "gawk", NULL
};

/* Commands that run another command: whatever they wrap is what matters, so
 * no rule can describe them. */
static const char *WRAPPERS[] = {
    "sudo", "env", "xargs", "nohup", "timeout", "nice",
    "npx", "eval", "source", ".", "exec",
    "time", "command", "builtin", "stdbuf", "chroot", "watch", "strace",
    "flock", "parallel", "setsid", "script",
    "dash", "ksh", "fish", "csh", "tcsh",
    "bunx", "uvx", NULL
};

/* Subcommands that fetch, compile or containerise and then run an arbitrary
 * program. */
static const char *WRAPPER_SUBCOMMANDS[][2] = {
    {"go", "run"},
    {"npm", "x"}, {"npm", "exec"},
    {"pnpm", "dlx"}, {"pnpm", "exec"},
    {"yarn", "dlx"}, {"yarn", "exec"},
    {"pnpm", "node"}, {"yarn", "node"},
    {"uv", "run"}, {"poetry", "run"}, {"pipx", "run"}, {"bundle", "exec"},
    {"docker", "run"}, {"docker", "exec"},
    {"kubectl", "run"}, {"kubectl", "exec"},
    {NULL, NULL}
};

/*
 * Subcommands no rule can describe: they run code the rule can't see, or they
 * change state that can't be restored. Both are decided per command, so the
 * prompt can name the real reason.
 */
typedef struct {
    const char     *command;
    const char     *subcommand;  /* NULL = every subcommand, and the bare command */
    ShellRuleStatus status;
} UnsaveableSubcommand;

static const UnsaveableSubcommand UNSAVEABLE_SUBCOMMANDS[] = {
    {"git", "config", SHELL_RULE_RUNS_CODE},
    {"git", "rebase", SHELL_RULE_RUNS_CODE},
    {"git", "filter-branch", SHELL_RULE_RUNS_CODE},
    {"git", "submodule", SHELL_RULE_RUNS_CODE},
    {"git", "worktree", SHELL_RULE_RUNS_CODE},
    {"git", "checkout", SHELL_RULE_IRREVERSIBLE},
    {"git", "restore", SHELL_RULE_IRREVERSIBLE},
    {"git", "reset", SHELL_RULE_IRREVERSIBLE},
    {"git", "clean", SHELL_RULE_IRREVERSIBLE},
    {"git", "stash", SHELL_RULE_IRREVERSIBLE},
    {"docker", "build", SHELL_RULE_RUNS_CODE},
    {"docker", "compose", SHELL_RULE_RUNS_CODE},
    {"kubectl", "apply", SHELL_RULE_IRREVERSIBLE},
    {"kubectl", "delete", SHELL_RULE_IRREVERSIBLE},
    /* gh runs user-defined aliases and extensions, and gh api writes. */
    {"gh", NULL, SHELL_RULE_RUNS_CODE},
    {NULL, NULL, SHELL_RULE_OK}
};

/*
 * Commands that run a named script or target. The name belongs in the rule, so
 * "npm run build" never covers "npm run deploy".
 */
typedef struct {
    const char *command;
    const char *subcommand;    /* NULL = the subcommand is itself the target */
    int         names_script;  /* 1 = the operand after the subcommand is the script */
} ScriptRunner;

static const ScriptRunner SCRIPT_RUNNERS[] = {
    {"npm", "run", 1},
    {"npm", "run-script", 1},
    {"npm", "test", 0},
    {"pnpm", "run", 1},
    {"pnpm", "run-script", 1},
    {"yarn", "run", 1},
    {"yarn", NULL, 0},
    {"make", NULL, 0},
    {NULL, NULL, 0}
};

/* Package managers whose run/exec/test covers whatever the project defines. */
static const char *PACKAGE_MANAGERS[] = {
    "npm", "pnpm", "yarn", "bun", "uv", "poetry", "pipx", "bundle", NULL
};

/* Shells that run a command when given -c. */
static const char *SHELLS[] = {
    "bash", "sh", "zsh", NULL
};

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static int in_list(const char *s, const char *const *list) {
    for (int i = 0; list[i] != NULL; i++) {
        if (strcmp(s, list[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/*
 * How a command is looked up in the tables: by the part after the last '/', and
 * by that name with a trailing version suffix removed, so "/usr/bin/python3.12"
 * is also "python".
 */
typedef struct {
    const char *base;
    char        stripped[64]; /* "" when there is no version suffix to remove */
} CommandName;

static CommandName command_name_of(const char *command) {
    const char *slash = strrchr(command, '/');
    CommandName name = { slash != NULL ? slash + 1 : command, {0} };

    size_t len = strlen(name.base);
    size_t end = len;
    while (end > 1 && (isdigit((unsigned char)name.base[end - 1]) ||
                       name.base[end - 1] == '.')) {
        end--;
    }
    if (end < len && end < sizeof(name.stripped)) {
        memcpy(name.stripped, name.base, end);
        name.stripped[end] = '\0';
    }
    return name;
}

static int command_name_is(const CommandName *name, const char *candidate) {
    return strcmp(name->base, candidate) == 0 ||
           (name->stripped[0] != '\0' && strcmp(name->stripped, candidate) == 0);
}

static int command_name_in_list(const CommandName *name, const char *const *list) {
    if (in_list(name->base, list)) {
        return 1;
    }
    return name->stripped[0] != '\0' && in_list(name->stripped, list);
}

static int is_wrapper_subcommand(const CommandName *name, const char *subcommand) {
    if (subcommand == NULL) {
        return 0;
    }
    for (int i = 0; WRAPPER_SUBCOMMANDS[i][0] != NULL; i++) {
        if (command_name_is(name, WRAPPER_SUBCOMMANDS[i][0]) &&
            strcmp(subcommand, WRAPPER_SUBCOMMANDS[i][1]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Whether any subcommand of the command runs whatever it is given. */
static int has_wrapper_subcommands(const CommandName *name) {
    for (int i = 0; WRAPPER_SUBCOMMANDS[i][0] != NULL; i++) {
        if (command_name_is(name, WRAPPER_SUBCOMMANDS[i][0])) {
            return 1;
        }
    }
    return 0;
}

/* SHELL_RULE_OK when the command and subcommand can have a rule at all. */
static ShellRuleStatus unsaveable_subcommand_status(const CommandName *name,
                                                    const char *subcommand) {
    for (int i = 0; UNSAVEABLE_SUBCOMMANDS[i].command != NULL; i++) {
        const UnsaveableSubcommand *e = &UNSAVEABLE_SUBCOMMANDS[i];
        if (!command_name_is(name, e->command)) {
            continue;
        }
        if (e->subcommand == NULL ||
            (subcommand != NULL && strcmp(e->subcommand, subcommand) == 0)) {
            return e->status;
        }
    }
    return SHELL_RULE_OK;
}

static const ScriptRunner *find_script_runner(const CommandName *name,
                                              const char *subcommand) {
    for (int i = 0; SCRIPT_RUNNERS[i].command != NULL; i++) {
        const ScriptRunner *e = &SCRIPT_RUNNERS[i];
        if (!command_name_is(name, e->command)) {
            continue;
        }
        if (e->subcommand == NULL ||
            (subcommand != NULL && strcmp(e->subcommand, subcommand) == 0)) {
            return e;
        }
    }
    return NULL;
}

/* A "NAME=value" prefix changes what the command sees (PATH, LD_PRELOAD,
 * GIT_*) and what it therefore runs. */
static int is_assignment(const char *t) {
    if (!isalpha((unsigned char)t[0]) && t[0] != '_') {
        return 0;
    }
    const char *p = t + 1;
    while (isalnum((unsigned char)*p) || *p == '_') {
        p++;
    }
    return *p == '=';
}

static int is_all_digits(const char *s) {
    if (*s == '\0') {
        return 0;
    }
    for (; *s != '\0'; s++) {
        if (!isdigit((unsigned char)*s)) {
            return 0;
        }
    }
    return 1;
}

/* An option token: starts with '-' and is not a bare "-". */
static int is_option(const char *t) {
    return t[0] == '-' && t[1] != '\0';
}

/* Combined short flags like -fu, which stand for -f -u. */
static int is_short_cluster(const char *t) {
    return t[0] == '-' && t[1] != '-' && t[1] != '\0' && t[2] != '\0';
}

static const SubcommandTool *find_subcommand_tool(const CommandName *name) {
    for (int i = 0; SUBCOMMAND_TOOLS[i].command != NULL; i++) {
        if (command_name_is(name, SUBCOMMAND_TOOLS[i].command)) {
            return &SUBCOMMAND_TOOLS[i];
        }
    }
    return NULL;
}

/* Matches "--name" and "--name=value". */
static const GlobalOpt *find_global_opt(const GlobalOpt *options, const char *token) {
    for (int i = 0; options[i].name != NULL; i++) {
        size_t len = strlen(options[i].name);
        if (strcmp(token, options[i].name) == 0 ||
            (strncmp(token, options[i].name, len) == 0 && token[len] == '=')) {
            return &options[i];
        }
    }
    return NULL;
}

/*
 * Find the subcommand, skipping the tool's known global options.
 * Returns 0 with *out_sub set to the subcommand or NULL when there is none,
 * or -1 with *out_detail set to an option we can't classify.
 */
static int resolve_subcommand(const ParsedShellCommand *p, const SubcommandTool *tool,
                              const char **out_sub, const char **out_detail) {
    *out_sub = NULL;
    for (int i = 1; i < p->token_count; i++) {
        const char *t = p->tokens[i];
        if (strcmp(t, "--") == 0) {
            return 0;
        }
        if (!is_option(t)) {
            if (tool->assignments && strchr(t, '=') != NULL) {
                continue;
            }
            *out_sub = t;
            return 0;
        }

        const GlobalOpt *opt = find_global_opt(tool->options, t);
        if (opt == NULL) {
            *out_detail = t;
            return -1;
        }
        int has_inline_value = strchr(t, '=') != NULL;
        int next_is_number = i + 1 < p->token_count && is_all_digits(p->tokens[i + 1]);
        if ((opt->kind == OPT_VALUE && !has_inline_value) ||
            (opt->kind == OPT_NUMBER && next_is_number)) {
            i++;
        }
    }
    return 0;
}

/*
 * Find the always-ask entry a token matches.
 *
 * anywhere_only limits the search to entries that act even after "--".
 * subcommand_known == 0 means a global option left the subcommand
 * unresolved: scoped entries then match any subcommand, so the check fails
 * closed instead of waving a flag through.
 *
 * allow_abbrev accepts a shortened long option as the flag it shortens, the way
 * git and getopt_long read options: "git reset --har" is "git reset --hard".
 * Commands that don't accept abbreviations only fail closed.
 */
static const AlwaysAskFlag *match_always_ask(const CommandName *name,
                                             const char *subcommand,
                                             int subcommand_known, const char *token,
                                             int anywhere_only, int allow_abbrev) {
    size_t abbrev_len = strcspn(token, "=");
    int abbreviates = allow_abbrev && token[0] == '-' && token[1] == '-' &&
                      abbrev_len > 2;

    for (int i = 0; ALWAYS_ASK_FLAGS[i].flag != NULL; i++) {
        const AlwaysAskFlag *e = &ALWAYS_ASK_FLAGS[i];
        if (anywhere_only && !e->anywhere) {
            continue;
        }
        if (e->command != NULL && !command_name_is(name, e->command)) {
            continue;
        }
        if (e->subcommand != NULL && subcommand_known &&
            (subcommand == NULL || strcmp(e->subcommand, subcommand) != 0)) {
            continue;
        }
        if (e->prefix ? strncmp(token, e->flag, strlen(e->flag)) == 0
                      : strcmp(token, e->flag) == 0) {
            return e;
        }
        if (abbreviates && e->flag[0] == '-' && e->flag[1] == '-' &&
            strncmp(token, e->flag, abbrev_len) == 0) {
            return e;
        }
    }
    return NULL;
}

/*
 * Dangerous git operations that take more than one token to recognise: a push
 * refspec starting with '+' force-updates and one starting with ':' deletes,
 * and "git branch --delete --force" is "git branch -D" spelled out.
 */
static int git_spelling_always_ask(const ParsedShellCommand *p, const char *subcommand,
                                   const char **out_flag) {
    if (subcommand == NULL) {
        return 0;
    }

    if (strcmp(subcommand, "push") == 0) {
        for (int i = 1; i < p->token_count; i++) {
            const char *t = p->tokens[i];
            if (!is_option(t) && (t[0] == '+' || t[0] == ':')) {
                if (out_flag != NULL) {
                    *out_flag = t;
                }
                return 1;
            }
        }
        return 0;
    }

    if (strcmp(subcommand, "branch") == 0) {
        const char *del = NULL;
        const char *force = NULL;
        for (int i = 1; i < p->token_count; i++) {
            const char *t = p->tokens[i];
            if (!is_option(t)) {
                continue;
            }
            if (strcmp(t, "--delete") == 0) {
                del = t;
            } else if (strcmp(t, "--force") == 0) {
                force = t;
            } else if (t[1] != '-') {
                for (const char *c = t + 1; *c != '\0'; c++) {
                    if (*c == 'd') {
                        del = t;
                    } else if (*c == 'f') {
                        force = t;
                    }
                }
            }
        }
        if (del != NULL && force != NULL) {
            if (out_flag != NULL) {
                *out_flag = force;
            }
            return 1;
        }
    }
    return 0;
}

/*
 * The script a runner was told to run: the token right after the subcommand.
 * Anything else there (an option, "--") means the script isn't named where a
 * rule could pin it, so there is none.
 */
static const char *script_token_after(const ParsedShellCommand *p,
                                      const char *subcommand) {
    for (int i = 1; i + 1 < p->token_count; i++) {
        if (p->tokens[i] == subcommand) {
            const char *next = p->tokens[i + 1];
            return is_option(next) ? NULL : next;
        }
    }
    return NULL;
}

static int build_rule(ShellRule *out, char *const *tokens, int count, int exact) {
    out->tokens = calloc((size_t)count, sizeof(char *));
    if (out->tokens == NULL) {
        return -1;
    }
    for (int i = 0; i < count; i++) {
        out->tokens[i] = strdup(tokens[i]);
        if (out->tokens[i] == NULL) {
            out->count = i;
            shell_rule_free(out);
            return -1;
        }
    }
    out->count = count;
    out->exact = exact;
    return 0;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

int shell_has_always_ask_flag(const ParsedShellCommand *p, const char **out_flag) {
    if (out_flag != NULL) {
        *out_flag = NULL;
    }
    if (p == NULL || p->token_count < 1) {
        return 0;
    }

    CommandName name = command_name_of(p->tokens[0]);
    const char *subcommand = NULL;
    const char *detail = NULL;
    int subcommand_known = 1;
    const SubcommandTool *tool = find_subcommand_tool(&name);
    if (tool != NULL) {
        subcommand_known = resolve_subcommand(p, tool, &subcommand, &detail) == 0;
    }
    /* git spells some dangerous operations out across several tokens. */
    int is_git = command_name_is(&name, "git");

    /* Flags that write files or run programs (find's actions, sed -i, --output)
     * are honoured wherever they appear: a command can place them after "--"
     * and they still act. Only the git-style flags stop at "--", where they
     * really are operands. */
    int operands_only = 0;

    for (int i = 1; i < p->token_count; i++) {
        const char *t = p->tokens[i];
        if (!is_option(t)) {
            continue;
        }
        const AlwaysAskFlag *hit = match_always_ask(&name, subcommand, subcommand_known,
                                                   t, operands_only, 1);
        const char *offender = t;
        if (hit == NULL && is_short_cluster(t)) {
            for (const char *c = t + 1; *c != '\0' && hit == NULL; c++) {
                char single[3] = { '-', *c, '\0' };
                hit = match_always_ask(&name, subcommand, subcommand_known, single,
                                       operands_only, 0);
                offender = hit != NULL ? hit->flag : t;
            }
        }
        if (hit != NULL) {
            if (out_flag != NULL) {
                *out_flag = offender;
            }
            return 1;
        }
        if (strcmp(t, "--") == 0) {
            operands_only = 1;
        }
    }

    if (is_git && git_spelling_always_ask(p, subcommand, out_flag)) {
        return 1;
    }

    /* A global option we can't classify hides what the command really runs. */
    return !subcommand_known;
}

ShellRuleStatus shell_rule_from_parsed(const ParsedShellCommand *p, ShellRule *out,
                                       const char **out_detail) {
    if (out_detail != NULL) {
        *out_detail = NULL;
    }
    if (out != NULL) {
        out->tokens = NULL;
        out->count = 0;
        out->exact = 0;
    }
    if (p == NULL || out == NULL || p->token_count < 1) {
        return SHELL_RULE_UNSAFE_SYNTAX;
    }
    if (p->has_chain || p->has_pipe || p->has_subshell || p->is_dangerous) {
        return SHELL_RULE_UNSAFE_SYNTAX;
    }
    if (p->has_redirect) {
        return SHELL_RULE_REDIRECT;
    }

    if (is_assignment(p->tokens[0])) {
        return SHELL_RULE_WRAPPER;
    }

    const char *command = p->tokens[0];
    CommandName name = command_name_of(command);
    const SubcommandTool *tool = find_subcommand_tool(&name);
    const char *subcommand = NULL;
    if (tool != NULL) {
        const char *detail = NULL;
        if (resolve_subcommand(p, tool, &subcommand, &detail) != 0) {
            if (out_detail != NULL) {
                *out_detail = detail;
            }
            return SHELL_RULE_UNKNOWN_GLOBAL_OPT;
        }
    }

    const char *flag = NULL;
    if (shell_has_always_ask_flag(p, &flag)) {
        if (out_detail != NULL) {
            *out_detail = flag;
        }
        return SHELL_RULE_ALWAYS_ASK_FLAG;
    }

    if (command_name_in_list(&name, WRAPPERS) ||
        is_wrapper_subcommand(&name, subcommand)) {
        return SHELL_RULE_WRAPPER;
    }
    if (command_name_in_list(&name, SHELLS)) {
        for (int i = 1; i < p->token_count; i++) {
            const char *t = p->tokens[i];
            if (is_option(t) && t[1] != '-' && strchr(t + 1, 'c') != NULL) {
                return SHELL_RULE_WRAPPER;
            }
        }
    }

    ShellRuleStatus unsaveable = unsaveable_subcommand_status(&name, subcommand);
    if (unsaveable != SHELL_RULE_OK) {
        return unsaveable;
    }

    /* A VAR=value argument changes the recipes make runs. */
    if (tool != NULL && tool->assignments) {
        for (int i = 1; i < p->token_count; i++) {
            if (is_assignment(p->tokens[i])) {
                return SHELL_RULE_RUNS_CODE;
            }
        }
    }

    if (command_name_in_list(&name, NEVER_BROADEN)) {
        /* The second token is what the rule pins down. An option there (python -c,
         * node -e, curl -o) carries the real payload, so no rule can cover it. */
        if (p->token_count > 1 && is_option(p->tokens[1])) {
            return SHELL_RULE_WRAPPER;
        }
        int count = p->token_count < 2 ? p->token_count : 2;
        return build_rule(out, p->tokens, count, 1) == 0 ? SHELL_RULE_OK
                                                         : SHELL_RULE_UNSAFE_SYNTAX;
    }

    const ScriptRunner *runner = find_script_runner(&name, subcommand);
    if (runner != NULL && subcommand != NULL) {
        char *tokens[3] = { (char *)command, (char *)subcommand, NULL };
        int count = 2;
        if (runner->names_script) {
            const char *script = script_token_after(p, subcommand);
            if (script == NULL) {
                /* Without a script the rule would cover every script there is. */
                return SHELL_RULE_WRAPPER;
            }
            tokens[2] = (char *)script;
            count = 3;
        }
        /* Matched against the rule the command derives, so the global options a
         * runner takes (make -j4, make -C build) don't stop the rule covering
         * the very command it was saved from. */
        return build_rule(out, tokens, count, 0) == 0 ? SHELL_RULE_OK
                                                      : SHELL_RULE_UNSAFE_SYNTAX;
    }

    if (subcommand != NULL) {
        char *tokens[2] = { (char *)command, (char *)subcommand };
        return build_rule(out, tokens, 2, 0) == 0 ? SHELL_RULE_OK
                                                  : SHELL_RULE_UNSAFE_SYNTAX;
    }

    return build_rule(out, p->tokens, 1, 0) == 0 ? SHELL_RULE_OK : SHELL_RULE_UNSAFE_SYNTAX;
}

int shell_rule_matches(const ShellRule *rule, const ParsedShellCommand *p) {
    if (rule == NULL || rule->count < 1 || p == NULL) {
        return 0;
    }

    ShellRule derived;
    if (shell_rule_from_parsed(p, &derived, NULL) != SHELL_RULE_OK) {
        return 0;
    }

    int ok;
    if (rule->exact) {
        /* Legacy prefix semantics: the rule's tokens must open the command. */
        ok = rule->count <= p->token_count;
        for (int i = 0; ok && i < rule->count; i++) {
            ok = strcmp(rule->tokens[i], p->tokens[i]) == 0;
        }
    } else {
        ok = rule->count == derived.count;
        for (int i = 0; ok && i < rule->count; i++) {
            ok = strcmp(rule->tokens[i], derived.tokens[i]) == 0;
        }
    }

    shell_rule_free(&derived);
    return ok;
}

int shell_rule_is_broad(const char *const *tokens, int count) {
    if (tokens == NULL || count < 1 || tokens[0] == NULL) {
        return 0;
    }

    CommandName name = command_name_of(tokens[0]);
    if (command_name_in_list(&name, NEVER_BROADEN) ||
        command_name_in_list(&name, WRAPPERS)) {
        return 1;
    }

    /* A bare tool entry covers its own command-running subcommands. */
    if (count == 1) {
        return command_name_in_list(&name, PACKAGE_MANAGERS) ||
               has_wrapper_subcommands(&name);
    }

    if (tokens[1] != NULL) {
        /* Naming what the wrapper runs doesn't narrow the entry: it still
         * matches every flag and operand that follows. */
        if (is_wrapper_subcommand(&name, tokens[1])) {
            return 1;
        }
        if (count == 2 && command_name_in_list(&name, PACKAGE_MANAGERS)) {
            return strcmp(tokens[1], "run") == 0 || strcmp(tokens[1], "exec") == 0 ||
                   strcmp(tokens[1], "test") == 0;
        }
    }
    return 0;
}

const char *shell_rule_status_reason(ShellRuleStatus s) {
    switch (s) {
        case SHELL_RULE_UNSAFE_SYNTAX:
            return "runs a command inside quotes or a chain";
        case SHELL_RULE_REDIRECT:
            return "writes to a file";
        case SHELL_RULE_ALWAYS_ASK_FLAG:
            return "uses an always-ask flag";
        case SHELL_RULE_WRAPPER:
            return "runs another command";
        case SHELL_RULE_UNKNOWN_GLOBAL_OPT:
            return "unrecognised option before the subcommand";
        case SHELL_RULE_RUNS_CODE:
            return "can run arbitrary code";
        case SHELL_RULE_IRREVERSIBLE:
            return "can't be undone";
        case SHELL_RULE_OK:
            break;
    }
    return "";
}

char *shell_rule_label(const ShellRule *rule) {
    if (rule == NULL || rule->count < 1) {
        return NULL;
    }

    static const char *SUFFIX = " …";
    size_t len = strlen(SUFFIX) + 1;
    for (int i = 0; i < rule->count; i++) {
        len += strlen(rule->tokens[i]) + (i > 0 ? 1 : 0);
    }

    char *label = malloc(len);
    if (label == NULL) {
        return NULL;
    }
    label[0] = '\0';
    for (int i = 0; i < rule->count; i++) {
        if (i > 0) {
            strcat(label, " ");
        }
        strcat(label, rule->tokens[i]);
    }
    strcat(label, SUFFIX);
    return label;
}

void shell_rule_free(ShellRule *rule) {
    if (rule == NULL) {
        return;
    }
    for (int i = 0; i < rule->count; i++) {
        free(rule->tokens[i]);
    }
    free(rule->tokens);
    rule->tokens = NULL;
    rule->count = 0;
    rule->exact = 0;
}
