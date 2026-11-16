#include "../unity/unity.h"
#include "policy/shell_rules.h"
#include "policy/shell_parser.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static ParsedShellCommand *parse(const char *command) {
    ParsedShellCommand *p = parse_shell_command_for_type(command, SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(p);
    return p;
}

/* Derives a rule and asserts the status, returning the rule for inspection. */
static ShellRuleStatus rule_for(const char *command, ShellRule *out,
                                const char **out_detail) {
    ParsedShellCommand *p = parse(command);
    ShellRuleStatus s = shell_rule_from_parsed(p, out, out_detail);
    free_parsed_shell_command(p);
    return s;
}

static void assert_rule(const char *command, int exact, int count, ...) {
    ShellRule rule;
    const char *detail = NULL;
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_OK, rule_for(command, &rule, &detail));
    TEST_ASSERT_EQUAL_INT(exact, rule.exact);
    TEST_ASSERT_EQUAL_INT(count, rule.count);

    va_list args;
    va_start(args, count);
    for (int i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL_STRING(va_arg(args, const char *), rule.tokens[i]);
    }
    va_end(args);
    shell_rule_free(&rule);
}

static ShellRuleStatus status_of(const char *command) {
    ShellRule rule;
    const char *detail = NULL;
    ShellRuleStatus s = rule_for(command, &rule, &detail);
    if (s == SHELL_RULE_OK) {
        shell_rule_free(&rule);
    }
    return s;
}

static int matches(const char *rule_command, const char *command) {
    ShellRule rule;
    const char *detail = NULL;
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_OK, rule_for(rule_command, &rule, &detail));
    ParsedShellCommand *p = parse(command);
    int ok = shell_rule_matches(&rule, p);
    free_parsed_shell_command(p);
    shell_rule_free(&rule);
    return ok;
}

static int always_ask(const char *command) {
    ParsedShellCommand *p = parse(command);
    int ok = shell_has_always_ask_flag(p, NULL);
    free_parsed_shell_command(p);
    return ok;
}

void test_plain_command_rule(void) {
    assert_rule("grep -rn foo lib", 0, 1, "grep");
    assert_rule("ls", 0, 1, "ls");
}

void test_subcommand_rule_skips_global_options(void) {
    assert_rule("git -C lib show HEAD", 0, 2, "git", "show");
    assert_rule("git --no-pager log", 0, 2, "git", "log");
    assert_rule("git --git-dir=/x/.git status", 0, 2, "git", "status");
    assert_rule("docker --context remote ps", 0, 2, "docker", "ps");
    assert_rule("kubectl -n kube-system get pods", 0, 2, "kubectl", "get");
}

void test_table_lookups_use_the_base_name(void) {
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("/usr/bin/git push --force"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_RUNS_CODE, status_of("/usr/bin/git config user.name x"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("/usr/local/bin/docker run x"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("/usr/bin/time ls"));

    /* The saved tokens stay as the user typed them. */
    assert_rule("/usr/bin/git show HEAD", 0, 2, "/usr/bin/git", "show");
}

void test_code_running_subcommands_are_unsaveable(void) {
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_RUNS_CODE, status_of("git config user.name x"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_RUNS_CODE, status_of("git rebase main"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_RUNS_CODE, status_of("git filter-branch"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_RUNS_CODE, status_of("git submodule update"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_RUNS_CODE, status_of("git worktree list"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_RUNS_CODE, status_of("docker build ."));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_RUNS_CODE, status_of("docker compose up"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_RUNS_CODE, status_of("gh pr list"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_RUNS_CODE, status_of("gh"));

    TEST_ASSERT_EQUAL_INT(SHELL_RULE_IRREVERSIBLE, status_of("git checkout main"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_IRREVERSIBLE, status_of("git restore lib"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_IRREVERSIBLE, status_of("git reset HEAD~1"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_IRREVERSIBLE, status_of("git clean -n"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_IRREVERSIBLE, status_of("git stash pop"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_IRREVERSIBLE, status_of("kubectl apply -f x.yaml"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_IRREVERSIBLE, status_of("kubectl delete pod x"));

    /* Harmless subcommands of the same tools still save a rule. */
    assert_rule("docker ps", 0, 2, "docker", "ps");
    assert_rule("git log --oneline", 0, 2, "git", "log");
}

void test_command_runners_are_wrappers(void) {
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("docker run x"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("docker exec c ls"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("kubectl exec pod -- ls"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("kubectl run x --image y"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("npm x prettier"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("pnpm exec prettier"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("yarn exec prettier"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("uv run pytest"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("poetry run pytest"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("pipx run black"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("bundle exec rspec"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("bunx prettier"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("uvx ruff"));

    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("time ls"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("command ls"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("builtin cd x"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("stdbuf -o0 ls"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("chroot /x ls"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("watch ls"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("strace ls"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("flock /tmp/l ls"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("parallel ls"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("setsid ls"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("script out.txt"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("dash x.sh"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("ksh x.sh"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("fish x.fish"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("csh x.csh"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("tcsh x.csh"));
}

void test_script_rules_name_the_script(void) {
    assert_rule("npm run test", 1, 3, "npm", "run", "test");
    TEST_ASSERT_EQUAL_INT(1, matches("npm run test", "npm run test -- -w"));
    TEST_ASSERT_EQUAL_INT(0, matches("npm run test", "npm run build"));
    assert_rule("npm test", 1, 2, "npm", "test");
    assert_rule("pnpm run build", 1, 3, "pnpm", "run", "build");
    assert_rule("yarn run build", 1, 3, "yarn", "run", "build");
    assert_rule("yarn build", 1, 2, "yarn", "build");

    /* A runner with no script names nothing to pin the rule to. */
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("npm run"));
}

void test_make_rules_name_the_target(void) {
    assert_rule("make test", 1, 2, "make", "test");
    TEST_ASSERT_EQUAL_INT(1, matches("make test", "make test"));
    TEST_ASSERT_EQUAL_INT(0, matches("make test", "make -f e.mk test"));
    TEST_ASSERT_EQUAL_INT(0, matches("make test", "make SHELL=x test"));
    TEST_ASSERT_EQUAL_INT(0, matches("make test", "make install"));

    /* A bare make covers only a bare make. */
    assert_rule("make", 0, 1, "make");
    TEST_ASSERT_EQUAL_INT(0, matches("make", "make install"));

    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("make CC=gcc all"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("make -f e.mk test"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("make --file=e.mk test"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("make --eval=x test"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("make -E x test"));
}

void test_unknown_global_option_is_unsaveable(void) {
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_UNKNOWN_GLOBAL_OPT, status_of("git --weird x status"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_UNKNOWN_GLOBAL_OPT, status_of("cargo --weird build"));
}

void test_always_ask_flags_block_rules(void) {
    ShellRule rule;
    const char *detail = NULL;
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG,
                          rule_for("git -c core.pager=x show", &rule, &detail));
    TEST_ASSERT_EQUAL_STRING("-c", detail);

    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git push -fu origin main"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git push origin main -f"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG,
                          status_of("git push --force-with-lease origin main"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git reset --hard HEAD~1"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git clean -fd"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git checkout -- lib"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git branch -D topic"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("sed -i.bak s/a/b/ f"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("find . -execdir rm {} +"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("grep --output=x foo lib"));
}

void test_tokens_after_double_dash_are_operands(void) {
    assert_rule("git push origin main -- -f", 0, 2, "git", "push");
    /* Flags that write or run programs are the exception: see
     * test_always_ask_flag_scope. */
    assert_rule("grep -rn foo -- -f", 0, 1, "grep");
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("grep -rn foo -- --output"));
}

void test_never_broaden_commands_with_option_payloads_are_unsaveable(void) {
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("python -c 'x'"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("node -e 'x'"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("curl -o f url"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("rm -f x"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("ssh -t host ls"));
}

void test_git_always_ask_additions(void) {
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git push origin --mirror"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git push origin --prune"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git branch -d merged"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git branch --delete merged"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git branch -m new"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git branch -M new"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git branch --move new"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git branch -c copy"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git branch -C copy"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git tag -d v1"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git tag --delete v1"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git tag -f v1"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git tag --force v1"));

    /* Reading branches and tags still saves a rule. */
    assert_rule("git branch --list", 0, 2, "git", "branch");
    assert_rule("git tag --list", 0, 2, "git", "tag");
}

void test_git_long_option_abbreviations_always_ask(void) {
    TEST_ASSERT_EQUAL_INT(1, always_ask("git reset --har"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("git grep --open=x foo"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("git push --forc origin main"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("git push --mir origin"));

    /* Options that abbreviate nothing dangerous still pass. */
    TEST_ASSERT_EQUAL_INT(0, always_ask("git log --oneline"));
    TEST_ASSERT_EQUAL_INT(0, always_ask("git push origin main"));
    TEST_ASSERT_EQUAL_INT(0, always_ask("git push origin main -- -f"));
}

void test_destructive_flags_on_plain_commands_always_ask(void) {
    TEST_ASSERT_EQUAL_INT(1, always_ask("rm -r dir"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("rm -R dir"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("rm --recursive dir"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("rm --force x"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("rm foo -rf x"));
    TEST_ASSERT_EQUAL_INT(0, matches("rm foo", "rm foo -rf x"));

    TEST_ASSERT_EQUAL_INT(1, always_ask("curl https://x -o f"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("curl https://x -O"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("curl https://x --remote-name"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("curl https://x -T f"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("curl https://x --upload-file f"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("curl https://x -K cfg"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("curl https://x --config cfg"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("curl https://x --next https://y"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("curl https://x -d name=1"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("curl https://x --data-binary @f"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("curl https://x -F f=@g"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("curl https://x --form f=@g"));
    TEST_ASSERT_EQUAL_INT(0, matches("curl https://x", "curl https://x -o f"));

    TEST_ASSERT_EQUAL_INT(1, always_ask("wget https://x -O f"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("wget https://x --output-document f"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("wget https://x -P dir"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("wget https://x --post-file f"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("wget https://x --post-data x=1"));

    TEST_ASSERT_EQUAL_INT(1, always_ask("tar -xf a.tar --to-command cat"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("tar -xf a.tar --use-compress-program cat"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("tar -I cat -xf a.tar"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("tar -xf a.tar --checkpoint-action exec=x"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("tar -xf a.tar --info-script x.sh"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("tar -xf a.tar -F x.sh"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("tar -xf a.tar --new-volume-script x.sh"));
    TEST_ASSERT_EQUAL_INT(0, always_ask("tar -tf a.tar"));
}

void test_broad_rules_are_recognised(void) {
    const char *python[] = { "python" };
    const char *usr_python[] = { "/usr/bin/python3" };
    const char *npm_run[] = { "npm", "run" };
    const char *npm_run_build[] = { "npm", "run", "build" };
    const char *sudo[] = { "sudo" };
    const char *git_status[] = { "git", "status" };

    TEST_ASSERT_EQUAL_INT(1, shell_rule_is_broad(python, 1));
    TEST_ASSERT_EQUAL_INT(1, shell_rule_is_broad(usr_python, 1));
    TEST_ASSERT_EQUAL_INT(1, shell_rule_is_broad(npm_run, 2));
    TEST_ASSERT_EQUAL_INT(1, shell_rule_is_broad(sudo, 1));
    TEST_ASSERT_EQUAL_INT(0, shell_rule_is_broad(npm_run_build, 3));
    TEST_ASSERT_EQUAL_INT(0, shell_rule_is_broad(git_status, 2));
}

void test_never_broaden_commands_keep_two_token_rules(void) {
    assert_rule("python foo.py --x", 1, 2, "python", "foo.py");
    assert_rule("bash x.sh", 1, 2, "bash", "x.sh");
    assert_rule("curl https://example.com", 1, 2, "curl", "https://example.com");
    assert_rule("rm", 1, 1, "rm");
}

void test_wrappers_are_unsaveable(void) {
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("sudo ls"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("xargs rm"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("env A=1 x"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("nohup x"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("timeout 5 x"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("nice x"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("bash -c \"ls\""));
}

void test_unsafe_syntax_and_redirects(void) {
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_REDIRECT, status_of("ls > out"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_UNSAFE_SYNTAX, status_of("echo \"$(id)\""));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_UNSAFE_SYNTAX, status_of("ls | wc -l"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_UNSAFE_SYNTAX, status_of("ls; rm -rf /"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_UNSAFE_SYNTAX, status_of(""));
}

void test_matching_ignores_flag_order_and_global_options(void) {
    TEST_ASSERT_EQUAL_INT(1, matches("grep -rn foo lib", "grep -nr x"));
    TEST_ASSERT_EQUAL_INT(1, matches("grep -rn foo lib", "grep -v x"));
    TEST_ASSERT_EQUAL_INT(1, matches("git show", "git -C x show --stat"));
    TEST_ASSERT_EQUAL_INT(0, matches("git show", "git log"));
    TEST_ASSERT_EQUAL_INT(0, matches("git show", "grep x"));
    TEST_ASSERT_EQUAL_INT(1, matches("git push origin main", "git push upstream topic"));
    TEST_ASSERT_EQUAL_INT(0, matches("git push origin main", "git push -f"));
    TEST_ASSERT_EQUAL_INT(0, matches("grep foo", "grep foo > out"));
}

void test_matching_exact_rules_uses_prefix_semantics(void) {
    TEST_ASSERT_EQUAL_INT(1, matches("python foo.py", "python foo.py -v"));
    TEST_ASSERT_EQUAL_INT(0, matches("python foo.py", "python bar.py"));
    TEST_ASSERT_EQUAL_INT(0, matches("python foo.py", "python"));
}

void test_always_ask_flag_scope(void) {
    TEST_ASSERT_EQUAL_INT(1, always_ask("find . -delete"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("git push -f"));
    TEST_ASSERT_EQUAL_INT(0, always_ask("find . -name '*.c'"));

    /* Actions that write or run still act after "--"; git's flags don't. */
    TEST_ASSERT_EQUAL_INT(1, always_ask("find . -- -delete"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("find . -- -fls out"));
    TEST_ASSERT_EQUAL_INT(0, always_ask("git push origin main -- -f"));

    /* An unclassifiable global option hides the subcommand: fail closed. */
    TEST_ASSERT_EQUAL_INT(1, always_ask("git --weird x push -f"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("git --weird x status"));

    const char *flag = NULL;
    ParsedShellCommand *p = parse("find . -fprintf out");
    TEST_ASSERT_EQUAL_INT(1, shell_has_always_ask_flag(p, &flag));
    TEST_ASSERT_EQUAL_STRING("-fprintf", flag);
    free_parsed_shell_command(p);

    /* A cluster reports the single offending flag, not the whole cluster. */
    p = parse("git push -fu origin main");
    TEST_ASSERT_EQUAL_INT(1, shell_has_always_ask_flag(p, &flag));
    TEST_ASSERT_EQUAL_STRING("-f", flag);
    free_parsed_shell_command(p);
}

void test_git_grep_pager_flags_always_ask(void) {
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git grep -Ocat foo"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git grep -O cat foo"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG,
                          status_of("git grep --open-files-in-pager=cat foo"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG,
                          status_of("git grep --open-files-in-pager foo"));

    /* The flags count wherever they appear, so the check also sees them on a
     * command whose rule was already refused for another reason. */
    TEST_ASSERT_EQUAL_INT(1, always_ask("git grep -Otouch foo"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("git grep -O touch foo"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("git grep --open-files-in-pager=touch foo"));

    /* An ordinary grep still saves a rule. */
    assert_rule("git grep -n foo", 0, 2, "git", "grep");
}

void test_environment_assignment_prefix_is_unsaveable(void) {
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("CI=1 npm test"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("PATH=/tmp/bin ls"));
    TEST_ASSERT_EQUAL_INT(0, matches("npm test", "CI=1 npm test"));

    /* A command with an assignment prefix matches no rule at all. */
    char *tokens[1] = { (char *)"CI=1" };
    ShellRule rule = { .tokens = tokens, .count = 1, .exact = 0 };
    ParsedShellCommand *p = parse("CI=1 python3 -c 'import os'");
    TEST_ASSERT_EQUAL_INT(0, shell_rule_matches(&rule, p));
    free_parsed_shell_command(p);
}

void test_interpreter_detection_uses_base_name(void) {
    TEST_ASSERT_EQUAL_INT(0, matches("python3.12 a.py", "python3.12 b.py"));
    TEST_ASSERT_EQUAL_INT(0, matches("/usr/bin/python3 a.py", "/usr/bin/python3 b.py"));
    TEST_ASSERT_EQUAL_INT(0, matches(".venv/bin/python a.py", ".venv/bin/python b.py"));
    TEST_ASSERT_EQUAL_INT(1, matches("python3.12 a.py", "python3.12 a.py -v"));
    assert_rule("/usr/bin/python3 a.py", 1, 2, "/usr/bin/python3", "a.py");

    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of(".venv/bin/python -c 'x'"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("/usr/bin/env python3 a.py"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("/bin/sh -c 'ls'"));
}

void test_code_running_commands_never_broaden(void) {
    assert_rule("sed s/a/b/ f", 1, 2, "sed", "s/a/b/");
    TEST_ASSERT_EQUAL_INT(0, matches("sed s/a/b/ f", "sed '1e touch /tmp/x' f"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("sed -n 1p f"));
    TEST_ASSERT_EQUAL_INT(0, matches("awk '{print}' f", "awk 'BEGIN{system(1)}' f"));
    TEST_ASSERT_EQUAL_INT(0, matches("gawk '{print}' f", "gawk 'BEGIN{system(1)}' f"));

    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("npx prettier x"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("eval true"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("source script.sh"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of(". script.sh"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("exec ls"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("go run ./cmd"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("npm exec prettier"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("pnpm dlx prettier"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_WRAPPER, status_of("yarn dlx prettier"));

    /* Script runners name the script they run: see test_script_rules_name_the_script. */
    assert_rule("go build ./...", 0, 2, "go", "build");
}

void test_always_ask_spelling_variants(void) {
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git clean --force -dx"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git push origin +main"));
    TEST_ASSERT_EQUAL_INT(0, matches("git push origin main", "git push origin +main"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git push origin :topic"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git push --delete origin topic"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git push -d origin topic"));

    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git branch --delete --force x"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git branch -d -f x"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git branch --delete -f x"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git branch -df x"));

    /* Ordinary pushes still save a rule. */
    assert_rule("git push origin main", 0, 2, "git", "push");
    assert_rule("git push https://host/x.git main", 0, 2, "git", "push");
}

void test_status_reasons_and_labels(void) {
    TEST_ASSERT_EQUAL_STRING("writes to a file", shell_rule_status_reason(SHELL_RULE_REDIRECT));
    TEST_ASSERT_TRUE(strlen(shell_rule_status_reason(SHELL_RULE_WRAPPER)) > 0);
    TEST_ASSERT_TRUE(strlen(shell_rule_status_reason(SHELL_RULE_UNSAFE_SYNTAX)) > 0);
    TEST_ASSERT_TRUE(strlen(shell_rule_status_reason(SHELL_RULE_ALWAYS_ASK_FLAG)) > 0);
    TEST_ASSERT_TRUE(strlen(shell_rule_status_reason(SHELL_RULE_UNKNOWN_GLOBAL_OPT)) > 0);
    TEST_ASSERT_EQUAL_STRING("can run arbitrary code",
                             shell_rule_status_reason(SHELL_RULE_RUNS_CODE));
    TEST_ASSERT_EQUAL_STRING("can't be undone",
                             shell_rule_status_reason(SHELL_RULE_IRREVERSIBLE));

    ShellRule rule;
    const char *detail = NULL;
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_OK, rule_for("git show HEAD", &rule, &detail));
    char *label = shell_rule_label(&rule);
    TEST_ASSERT_EQUAL_STRING("git show …", label);
    free(label);
    shell_rule_free(&rule);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_plain_command_rule);
    RUN_TEST(test_subcommand_rule_skips_global_options);
    RUN_TEST(test_table_lookups_use_the_base_name);
    RUN_TEST(test_code_running_subcommands_are_unsaveable);
    RUN_TEST(test_command_runners_are_wrappers);
    RUN_TEST(test_script_rules_name_the_script);
    RUN_TEST(test_make_rules_name_the_target);
    RUN_TEST(test_git_always_ask_additions);
    RUN_TEST(test_git_long_option_abbreviations_always_ask);
    RUN_TEST(test_destructive_flags_on_plain_commands_always_ask);
    RUN_TEST(test_broad_rules_are_recognised);
    RUN_TEST(test_unknown_global_option_is_unsaveable);
    RUN_TEST(test_always_ask_flags_block_rules);
    RUN_TEST(test_tokens_after_double_dash_are_operands);
    RUN_TEST(test_never_broaden_commands_keep_two_token_rules);
    RUN_TEST(test_never_broaden_commands_with_option_payloads_are_unsaveable);
    RUN_TEST(test_wrappers_are_unsaveable);
    RUN_TEST(test_unsafe_syntax_and_redirects);
    RUN_TEST(test_matching_ignores_flag_order_and_global_options);
    RUN_TEST(test_matching_exact_rules_uses_prefix_semantics);
    RUN_TEST(test_always_ask_flag_scope);
    RUN_TEST(test_git_grep_pager_flags_always_ask);
    RUN_TEST(test_environment_assignment_prefix_is_unsaveable);
    RUN_TEST(test_interpreter_detection_uses_base_name);
    RUN_TEST(test_code_running_commands_never_broaden);
    RUN_TEST(test_always_ask_spelling_variants);
    RUN_TEST(test_status_reasons_and_labels);
    return UNITY_END();
}
