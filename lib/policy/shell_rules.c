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
    int              assignments;  /* 1 = takes VAR=value arguments (make), which
                                    * are not the subcommand */
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

/*
 * Words that run the command following them. The always-ask tables are keyed on
 * the command, so these words and any NAME=value prefix are skipped before the
 * lookup; an option this table doesn't know hides what really runs, so the
 * segment then fails closed.
 */
typedef struct {
    const char      *name;
    const GlobalOpt *options;
    int              operand_prefix; /* words before the command (timeout's duration) */
} Wrapper;

static const GlobalOpt SUDO_OPTS[] = {
    {"-u", OPT_VALUE}, {"--user", OPT_VALUE},
    {"-g", OPT_VALUE}, {"--group", OPT_VALUE},
    {"-E", OPT_FLAG}, {"--preserve-env", OPT_FLAG},
    {"-H", OPT_FLAG}, {"--set-home", OPT_FLAG},
    {"-n", OPT_FLAG}, {"--non-interactive", OPT_FLAG},
    {"-k", OPT_FLAG}, {"--reset-timestamp", OPT_FLAG},
    {NULL, OPT_FLAG}
};

static const GlobalOpt ENV_OPTS[] = {
    {"-i", OPT_FLAG}, {"--ignore-environment", OPT_FLAG},
    {"-u", OPT_VALUE}, {"--unset", OPT_VALUE},
    {"-C", OPT_VALUE}, {"--chdir", OPT_VALUE},
    {"-0", OPT_FLAG}, {"--null", OPT_FLAG},
    {NULL, OPT_FLAG}
};

static const GlobalOpt NICE_OPTS[] = {
    {"-n", OPT_VALUE}, {"--adjustment", OPT_VALUE}, {NULL, OPT_FLAG}
};

static const GlobalOpt TIMEOUT_OPTS[] = {
    {"-s", OPT_VALUE}, {"--signal", OPT_VALUE},
    {"-k", OPT_VALUE}, {"--kill-after", OPT_VALUE},
    {"--preserve-status", OPT_FLAG}, {"--foreground", OPT_FLAG},
    {"-v", OPT_FLAG}, {"--verbose", OPT_FLAG},
    {NULL, OPT_FLAG}
};

static const GlobalOpt STDBUF_OPTS[] = {
    {"-i", OPT_VALUE}, {"--input", OPT_VALUE},
    {"-o", OPT_VALUE}, {"--output", OPT_VALUE},
    {"-e", OPT_VALUE}, {"--error", OPT_VALUE},
    {NULL, OPT_FLAG}
};

static const Wrapper WRAPPERS[] = {
    {"sudo", SUDO_OPTS, 0},
    {"doas", NO_OPTS, 0},
    {"xargs", NO_OPTS, 0},
    {"exec", NO_OPTS, 0},
    {"chroot", NO_OPTS, 1},
    {"flock", NO_OPTS, 1},
    {"watch", NO_OPTS, 0},
    {"strace", NO_OPTS, 0},
    {"script", NO_OPTS, 0},
    {"env", ENV_OPTS, 0},
    {"nice", NICE_OPTS, 0},
    {"timeout", TIMEOUT_OPTS, 1},
    {"stdbuf", STDBUF_OPTS, 0},
    {"command", NO_OPTS, 0},
    {"nohup", NO_OPTS, 0},
    {"time", NO_OPTS, 0},
    {"setsid", NO_OPTS, 0},
    {"ionice", NO_OPTS, 0},
    {NULL, NULL, 0}
};

/* Tools whose always-ask flags are scoped to a subcommand: the subcommand has
 * to be resolved before those flags can be checked. */
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
    {"sed", NULL, "--in-place", 1, 1},
    /* perl -i rewrites files in place, usually spelled as a short cluster. */
    {"perl", NULL, "-i", 1, 1},
    /* rsync --delete removes files at the destination. */
    {"rsync", NULL, "--delete", 1, 1},
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

/* ==========================================================================
 * Helpers
 * ========================================================================== */

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
 * or -1 when an option we can't classify hides it.
 */
static int resolve_subcommand(const ParsedShellCommand *p, const SubcommandTool *tool,
                              const char **out_sub) {
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

/* A "~" bash expands: at the start of a word, or after an "=" or ":" in it. */
static int has_tilde_expansion(const char *t) {
    if (t[0] == '~') {
        return 1;
    }
    for (const char *c = t + 1; *c != '\0'; c++) {
        if (*c == '~' && (c[-1] == '=' || c[-1] == ':')) {
            return 1;
        }
    }
    return 0;
}

/* The token the shell would rewrite before the command sees it. */
static const char *expanding_token(const ParsedShellCommand *p) {
    for (int i = 0; i < p->token_count; i++) {
        const char *t = p->tokens[i];
        if (has_tilde_expansion(t) || strpbrk(t, "$`*?[") != NULL ||
            shell_token_has_brace_expansion(t)) {
            return t;
        }
    }
    return NULL;
}

static int build_rule(ShellRule *out, char *const *tokens, int count) {
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
    return 0;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

int shell_token_has_brace_expansion(const char *token) {
    if (token == NULL) {
        return 0;
    }
    for (const char *open = strchr(token, '{'); open != NULL;
         open = strchr(open + 1, '{')) {
        for (const char *p = open + 1; *p != '\0' && *p != '{' && *p != '}'; p++) {
            if (*p == ',' || (p[0] == '.' && p[1] == '.')) {
                return 1;
            }
        }
    }
    return 0;
}

/* A NAME=value word placed before the command. */
static int is_assignment(const char *t) {
    const char *eq = strchr(t, '=');
    if (eq == NULL || eq == t) {
        return 0;
    }
    if (!isalpha((unsigned char)t[0]) && t[0] != '_') {
        return 0;
    }
    for (const char *c = t + 1; c < eq; c++) {
        if (!isalnum((unsigned char)*c) && *c != '_') {
            return 0;
        }
    }
    return 1;
}

static const Wrapper *find_wrapper(const CommandName *name) {
    for (int i = 0; WRAPPERS[i].name != NULL; i++) {
        if (command_name_is(name, WRAPPERS[i].name)) {
            return &WRAPPERS[i];
        }
    }
    return NULL;
}

/*
 * Index of the word that names the command, skipping assignment prefixes and
 * wrappers. Returns -1 when a wrapper's options can't be parsed or no command
 * word is left, which the caller treats as always-ask.
 */
static int command_word_index(char *const *tokens, int count) {
    int i = 0;
    while (i < count) {
        if (is_assignment(tokens[i])) {
            i++;
            continue;
        }

        CommandName name = command_name_of(tokens[i]);
        const Wrapper *wrapper = find_wrapper(&name);
        if (wrapper == NULL) {
            return i;
        }

        i++;
        int operands = wrapper->operand_prefix;
        while (i < count) {
            const char *t = tokens[i];
            if (strcmp(t, "--") == 0) {
                i++;
                break;
            }
            if (is_assignment(t)) {
                i++;
                continue;
            }
            if (!is_option(t)) {
                if (operands == 0) {
                    break;
                }
                operands--;
                i++;
                continue;
            }
            const GlobalOpt *opt = find_global_opt(wrapper->options, t);
            if (opt == NULL) {
                return -1;
            }
            if (opt->kind == OPT_VALUE && strchr(t, '=') == NULL) {
                i++;
            }
            i++;
        }
        if (operands > 0) {
            return -1;
        }
    }
    return i < count ? i : -1;
}

int shell_command_changes_directory(const ParsedShellCommand *p) {
    if (p == NULL || p->token_count < 1) {
        return 0;
    }
    int command_word = command_word_index(p->tokens, p->token_count);
    if (command_word < 0) {
        /* A wrapper we can't read through is always-ask anyway. */
        return 0;
    }
    CommandName name = command_name_of(p->tokens[command_word]);
    return command_name_is(&name, "cd") || command_name_is(&name, "chdir") ||
           command_name_is(&name, "pushd") || command_name_is(&name, "popd");
}

static int always_ask_in_command(const ParsedShellCommand *p, const char **out_flag) {
    CommandName name = command_name_of(p->tokens[0]);
    const char *subcommand = NULL;
    int subcommand_known = 1;
    const SubcommandTool *tool = find_subcommand_tool(&name);
    if (tool != NULL) {
        subcommand_known = resolve_subcommand(p, tool, &subcommand) == 0;
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

int shell_has_always_ask_flag(const ParsedShellCommand *p, const char **out_flag) {
    if (out_flag != NULL) {
        *out_flag = NULL;
    }
    if (p == NULL || p->token_count < 1) {
        return 0;
    }

    int command_word = command_word_index(p->tokens, p->token_count);
    if (command_word < 0) {
        /* A wrapper we can't read through: fail closed. */
        if (out_flag != NULL) {
            *out_flag = p->tokens[0];
        }
        return 1;
    }

    /* A borrowed view starting at the command word, so the tables are looked up
     * on the program that actually runs. */
    ParsedShellCommand command = *p;
    command.tokens = p->tokens + command_word;
    command.token_count = p->token_count - command_word;
    return always_ask_in_command(&command, out_flag);
}

ShellRuleStatus shell_rule_from_parsed(const ParsedShellCommand *p, ShellRule *out,
                                       const char **out_detail) {
    if (out_detail != NULL) {
        *out_detail = NULL;
    }
    if (out != NULL) {
        out->tokens = NULL;
        out->count = 0;
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
    if (shell_command_changes_directory(p)) {
        return SHELL_RULE_DIRECTORY_CHANGE;
    }

    const char *flag = NULL;
    if (shell_has_always_ask_flag(p, &flag)) {
        if (out_detail != NULL) {
            *out_detail = flag;
        }
        return SHELL_RULE_ALWAYS_ASK_FLAG;
    }

    /* The rule holds the tokens the shell produced, so a token the shell would
     * still rewrite stands for a command nobody reviewed. */
    const char *expanding = expanding_token(p);
    if (p->has_variable || expanding != NULL) {
        if (out_detail != NULL) {
            *out_detail = expanding;
        }
        return SHELL_RULE_EXPANSION;
    }

    return build_rule(out, p->tokens, p->token_count) == 0 ? SHELL_RULE_OK
                                                           : SHELL_RULE_UNSAFE_SYNTAX;
}

int shell_rule_matches(const ShellRule *rule, const ParsedShellCommand *p) {
    if (rule == NULL || rule->count < 1 || p == NULL ||
        rule->count != p->token_count) {
        return 0;
    }

    for (int i = 0; i < rule->count; i++) {
        if (strcmp(rule->tokens[i], p->tokens[i]) != 0) {
            return 0;
        }
    }

    /* The same tokens still have to be a command "a" could have saved. */
    ShellRule derived;
    if (shell_rule_from_parsed(p, &derived, NULL) != SHELL_RULE_OK) {
        return 0;
    }
    shell_rule_free(&derived);
    return 1;
}

const char *shell_rule_status_reason(ShellRuleStatus s) {
    switch (s) {
        case SHELL_RULE_UNSAFE_SYNTAX:
            return "runs a command inside quotes or a chain";
        case SHELL_RULE_REDIRECT:
            return "writes to a file";
        case SHELL_RULE_ALWAYS_ASK_FLAG:
            return "uses an always-ask flag";
        case SHELL_RULE_EXPANSION:
            return "the shell expands part of it";
        case SHELL_RULE_PROTECTED_FILE:
            return "names a protected file";
        case SHELL_RULE_DIRECTORY_CHANGE:
            return "changes the directory later commands run in";
        case SHELL_RULE_OK:
            break;
    }
    return "";
}

char *shell_rule_label(const ShellRule *rule) {
    if (rule == NULL || rule->count < 1) {
        return NULL;
    }

    size_t len = 3; /* two quotes and NUL */
    for (int i = 0; i < rule->count; i++) {
        len += strlen(rule->tokens[i]) + (i > 0 ? 1 : 0);
    }

    char *label = malloc(len);
    if (label == NULL) {
        return NULL;
    }
    label[0] = '"';
    label[1] = '\0';
    for (int i = 0; i < rule->count; i++) {
        if (i > 0) {
            strcat(label, " ");
        }
        strcat(label, rule->tokens[i]);
    }
    strcat(label, "\"");
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
}
