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
    int              assignments;  /* 1 = VAR=value before the subcommand (make) */
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
    {"-C", OPT_VALUE}, {"-f", OPT_VALUE}, {"-j", OPT_NUMBER}, {NULL, OPT_FLAG}
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
    {NULL, NULL, 0}
};

/*
 * Flags that change what a command does. A segment carrying one never creates
 * a rule and never matches one. for_read_only marks the flags that also make
 * an otherwise read-only command able to write files or run programs.
 */
typedef struct {
    const char *command;     /* NULL = any command */
    const char *subcommand;  /* NULL = any subcommand */
    const char *flag;
    int         prefix;      /* 1 = match any flag starting with this */
    int         for_read_only;
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
    /* git grep's pager flag runs its value through the shell, so it is never
     * read-only. The other read-only git subcommands can only run a program
     * through repo config (--ext-diff, --textconv), which needs an earlier,
     * separately approved config write. */
    {"git", "grep", "-O", 1, 1},
    {"git", "grep", "--open-files-in-pager", 1, 1},
    {"git", "checkout", "--force", 0, 0},
    {"git", "checkout", "-f", 0, 0},
    {"git", "checkout", "--", 0, 0},
    {"git", "branch", "-D", 0, 0},
    /* git -c and --exec-path can run arbitrary code through config and aliases. */
    {"git", NULL, "-c", 0, 1},
    {"git", NULL, "--exec-path", 1, 1},
    {"sed", NULL, "-i", 1, 1},
    /* find's actions run programs or write files; they are checked on every
     * command so a read-only command can never smuggle one in. */
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
    "npx", "eval", "source", ".", "exec", NULL
};

/* Subcommands that fetch or compile and then run an arbitrary program. */
static const char *WRAPPER_SUBCOMMANDS[][2] = {
    {"go", "run"}, {"npm", "exec"}, {"pnpm", "dlx"}, {"yarn", "dlx"}, {NULL, NULL}
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

/* The part after the last '/': "/usr/bin/python3" -> "python3". */
static const char *command_base_name(const char *command) {
    const char *slash = strrchr(command, '/');
    return slash != NULL ? slash + 1 : command;
}

/* Classify a command by its base name, also trying the name with a trailing
 * version suffix removed, so "/usr/bin/python3.12" counts as "python". */
static int command_in_list(const char *command, const char *const *list) {
    const char *name = command_base_name(command);
    if (in_list(name, list)) {
        return 1;
    }

    size_t len = strlen(name);
    while (len > 1 && (isdigit((unsigned char)name[len - 1]) || name[len - 1] == '.')) {
        len--;
    }
    char stripped[64];
    if (len == strlen(name) || len >= sizeof(stripped)) {
        return 0;
    }
    memcpy(stripped, name, len);
    stripped[len] = '\0';
    return in_list(stripped, list);
}

static int is_wrapper_subcommand(const char *command, const char *subcommand) {
    if (subcommand == NULL) {
        return 0;
    }
    const char *name = command_base_name(command);
    for (int i = 0; WRAPPER_SUBCOMMANDS[i][0] != NULL; i++) {
        if (strcmp(name, WRAPPER_SUBCOMMANDS[i][0]) == 0 &&
            strcmp(subcommand, WRAPPER_SUBCOMMANDS[i][1]) == 0) {
            return 1;
        }
    }
    return 0;
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

static const SubcommandTool *find_subcommand_tool(const char *command) {
    for (int i = 0; SUBCOMMAND_TOOLS[i].command != NULL; i++) {
        if (strcmp(command, SUBCOMMAND_TOOLS[i].command) == 0) {
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
 * for_read_only limits the search to entries that also make a read-only command
 * unsafe. subcommand_known == 0 means a global option left the subcommand
 * unresolved: scoped entries then match any subcommand, so the check fails
 * closed instead of waving a flag through.
 */
static const AlwaysAskFlag *match_always_ask(const char *command, const char *subcommand,
                                             int subcommand_known, const char *token,
                                             int for_read_only) {
    for (int i = 0; ALWAYS_ASK_FLAGS[i].flag != NULL; i++) {
        const AlwaysAskFlag *e = &ALWAYS_ASK_FLAGS[i];
        if (for_read_only && !e->for_read_only) {
            continue;
        }
        if (e->command != NULL && strcmp(e->command, command) != 0) {
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

int shell_has_always_ask_flag(const ParsedShellCommand *p, int for_read_only,
                              const char **out_flag) {
    if (out_flag != NULL) {
        *out_flag = NULL;
    }
    if (p == NULL || p->token_count < 1) {
        return 0;
    }

    const char *command = p->tokens[0];
    const char *subcommand = NULL;
    const char *detail = NULL;
    int subcommand_known = 1;
    const SubcommandTool *tool = find_subcommand_tool(command);
    if (tool != NULL) {
        subcommand_known = resolve_subcommand(p, tool, &subcommand, &detail) == 0;
    }

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
        int read_only_scope = for_read_only || operands_only;
        const AlwaysAskFlag *hit =
            match_always_ask(command, subcommand, subcommand_known, t, read_only_scope);
        const char *offender = t;
        if (hit == NULL && is_short_cluster(t)) {
            for (const char *c = t + 1; *c != '\0' && hit == NULL; c++) {
                char single[3] = { '-', *c, '\0' };
                hit = match_always_ask(command, subcommand, subcommand_known, single,
                                       read_only_scope);
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

    if (!for_read_only && strcmp(command, "git") == 0 &&
        git_spelling_always_ask(p, subcommand, out_flag)) {
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
    const SubcommandTool *tool = find_subcommand_tool(command);
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
    if (shell_has_always_ask_flag(p, 0, &flag)) {
        if (out_detail != NULL) {
            *out_detail = flag;
        }
        return SHELL_RULE_ALWAYS_ASK_FLAG;
    }

    if (command_in_list(command, WRAPPERS) || is_wrapper_subcommand(command, subcommand)) {
        return SHELL_RULE_WRAPPER;
    }
    if (command_in_list(command, SHELLS)) {
        for (int i = 1; i < p->token_count; i++) {
            const char *t = p->tokens[i];
            if (is_option(t) && t[1] != '-' && strchr(t + 1, 'c') != NULL) {
                return SHELL_RULE_WRAPPER;
            }
        }
    }

    if (command_in_list(command, NEVER_BROADEN)) {
        /* The second token is what the rule pins down. An option there (python -c,
         * node -e, curl -o) carries the real payload, so no rule can cover it. */
        if (p->token_count > 1 && is_option(p->tokens[1])) {
            return SHELL_RULE_WRAPPER;
        }
        int count = p->token_count < 2 ? p->token_count : 2;
        return build_rule(out, p->tokens, count, 1) == 0 ? SHELL_RULE_OK
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
