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

static void assert_rule(const char *command, int count, ...) {
    ShellRule rule;
    const char *detail = NULL;
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_OK, rule_for(command, &rule, &detail));
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

void test_rule_is_the_whole_segment(void) {
    assert_rule("make test", 2, "make", "test");
    assert_rule("grep -rn foo lib", 4, "grep", "-rn", "foo", "lib");
    assert_rule("ls", 1, "ls");

    /* Interpreters, wrappers, assignments and paths are saved as typed: the
     * rule covers that exact segment and nothing else. */
    assert_rule("python3 x.py", 2, "python3", "x.py");
    assert_rule("sudo ls", 2, "sudo", "ls");
    assert_rule("CI=1 npm test", 3, "CI=1", "npm", "test");
    assert_rule("/usr/bin/git show HEAD", 3, "/usr/bin/git", "show", "HEAD");
}

void test_rule_matches_only_the_same_tokens(void) {
    TEST_ASSERT_EQUAL_INT(1, matches("make test", "make test"));
    TEST_ASSERT_EQUAL_INT(1, matches("git status", "git status"));
    TEST_ASSERT_EQUAL_INT(0, matches("git status", "git status -sb"));
    TEST_ASSERT_EQUAL_INT(0, matches("make test", "make test install"));
    /* Whitespace is the parser's business, not the rule's. */
    TEST_ASSERT_EQUAL_INT(1, matches("make test", "make  test"));

    /* Flag order is part of the command. */
    TEST_ASSERT_EQUAL_INT(0, matches("grep -rn foo lib", "grep -nr foo lib"));
    TEST_ASSERT_EQUAL_INT(1, matches("grep -rn foo lib", "grep -rn foo lib"));

    /* Options the review never saw can't ride along. */
    TEST_ASSERT_EQUAL_INT(0, matches("ssh host", "ssh host -o ProxyCommand=x"));
    TEST_ASSERT_EQUAL_INT(0, matches("git fetch", "git fetch --upload-pack=x ."));
    TEST_ASSERT_EQUAL_INT(0, matches("git -C lib show HEAD", "git show --stat"));

    TEST_ASSERT_EQUAL_INT(1, matches("python3 x.py", "python3 x.py"));
    TEST_ASSERT_EQUAL_INT(0, matches("python3 x.py", "python3 y.py"));
    TEST_ASSERT_EQUAL_INT(0, matches("python3 x.py", "python3 x.py -v"));
    TEST_ASSERT_EQUAL_INT(0, matches("cat README.md", "cat .env"));
}

void test_expansions_are_unsaveable(void) {
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_EXPANSION, status_of("find . ${U:--exec} x"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_EXPANSION, status_of("cat *.txt"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_EXPANSION, status_of("cat .{a,b}"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_EXPANSION, status_of("cat .{a..c}"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_EXPANSION, status_of("ls dir?"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_EXPANSION, status_of("ls [abc]"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_EXPANSION, status_of("echo $HOME"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_UNSAFE_SYNTAX, status_of("echo `id`"));

    ShellRule rule;
    const char *detail = NULL;
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_EXPANSION, rule_for("cat *.txt", &rule, &detail));
    TEST_ASSERT_EQUAL_STRING("*.txt", detail);

    /* Braces in a quoted program are text, not an expansion. */
    assert_rule("awk '{print}' f", 3, "awk", "{print}", "f");
    assert_rule("sed '1{p}' f", 3, "sed", "1{p}", "f");

    /* An expanding command matches no rule either. */
    TEST_ASSERT_EQUAL_INT(0, matches("cat README.md", "cat READ*.md"));
}

void test_always_ask_flags_block_rules(void) {
    ShellRule rule;
    const char *detail = NULL;
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG,
                          rule_for("git -c core.pager=x show", &rule, &detail));
    TEST_ASSERT_EQUAL_STRING("-c", detail);

    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git push --force"));
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
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("rm -f x"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("curl -o f url"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("make -f e.mk test"));

    /* Reading the same tools still saves a rule. */
    assert_rule("git branch --list", 3, "git", "branch", "--list");
    assert_rule("git tag --list", 3, "git", "tag", "--list");
}

void test_table_lookups_use_the_base_name(void) {
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG,
                          status_of("/usr/bin/git push --force"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("/usr/bin/git reset --hard"));
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

    /* getopt_long accepts abbreviated long options, so the check must too. */
    TEST_ASSERT_EQUAL_INT(1, always_ask("rm foo --rec x"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("tar -xf a.tar --to-com=sh"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("wget https://x --post-f=f"));

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

void test_always_ask_flag_scope(void) {
    TEST_ASSERT_EQUAL_INT(1, always_ask("find . -delete"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("git push -f"));
    TEST_ASSERT_EQUAL_INT(0, always_ask("find . -name x.c"));

    /* Actions that write or run still act after "--"; git's flags don't. */
    TEST_ASSERT_EQUAL_INT(1, always_ask("find . -- -delete"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("find . -- -fls out"));
    TEST_ASSERT_EQUAL_INT(0, always_ask("git push origin main -- -f"));

    /* An unclassifiable global option hides the subcommand: fail closed. */
    TEST_ASSERT_EQUAL_INT(1, always_ask("git --weird x push -f"));
    TEST_ASSERT_EQUAL_INT(1, always_ask("git --weird x status"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git --weird x status"));

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

    /* An ordinary grep still saves a rule. */
    assert_rule("git grep -n foo", 4, "git", "grep", "-n", "foo");
}

void test_always_ask_spelling_variants(void) {
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git clean --force -dx"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git push origin +main"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git push origin :topic"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG,
                          status_of("git push --delete origin topic"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git push -d origin topic"));

    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG,
                          status_of("git branch --delete --force x"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git branch -d -f x"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git branch --delete -f x"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_ALWAYS_ASK_FLAG, status_of("git branch -df x"));

    /* Ordinary pushes still save a rule. */
    assert_rule("git push origin main", 4, "git", "push", "origin", "main");
}

void test_unsafe_syntax_and_redirects(void) {
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_REDIRECT, status_of("ls > out"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_UNSAFE_SYNTAX, status_of("echo \"$(id)\""));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_UNSAFE_SYNTAX, status_of("ls | wc -l"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_UNSAFE_SYNTAX, status_of("ls; rm -rf /"));
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_UNSAFE_SYNTAX, status_of(""));

    TEST_ASSERT_EQUAL_INT(0, matches("grep foo bar", "grep foo bar > out"));
}

void test_broad_entries_are_recognised(void) {
    const char *python[] = { "python" };
    const char *usr_python[] = { "/usr/bin/python3" };
    const char *npm_run[] = { "npm", "run" };
    const char *npm_run_build[] = { "npm", "run", "build" };
    const char *sudo[] = { "sudo" };
    const char *git_status[] = { "git", "status" };
    const char *npm_x[] = { "npm", "x" };
    const char *docker_run[] = { "docker", "run" };
    const char *kubectl_exec[] = { "kubectl", "exec" };
    const char *go_run[] = { "go", "run" };
    const char *npm[] = { "npm" };
    const char *docker[] = { "docker" };
    const char *uv[] = { "uv" };
    const char *git[] = { "git" };
    const char *docker_ps[] = { "docker", "ps" };
    const char *docker_run_alpine[] = { "docker", "run", "alpine" };
    const char *npm_exec_prettier[] = { "npm", "exec", "prettier" };

    TEST_ASSERT_EQUAL_INT(1, shell_rule_is_broad(python, 1));
    TEST_ASSERT_EQUAL_INT(1, shell_rule_is_broad(usr_python, 1));
    TEST_ASSERT_EQUAL_INT(1, shell_rule_is_broad(npm_run, 2));
    TEST_ASSERT_EQUAL_INT(1, shell_rule_is_broad(sudo, 1));
    TEST_ASSERT_EQUAL_INT(1, shell_rule_is_broad(npm_x, 2));
    TEST_ASSERT_EQUAL_INT(1, shell_rule_is_broad(docker_run, 2));
    TEST_ASSERT_EQUAL_INT(1, shell_rule_is_broad(kubectl_exec, 2));
    TEST_ASSERT_EQUAL_INT(1, shell_rule_is_broad(go_run, 2));
    TEST_ASSERT_EQUAL_INT(1, shell_rule_is_broad(npm, 1));
    TEST_ASSERT_EQUAL_INT(1, shell_rule_is_broad(docker, 1));
    TEST_ASSERT_EQUAL_INT(1, shell_rule_is_broad(uv, 1));
    TEST_ASSERT_EQUAL_INT(0, shell_rule_is_broad(npm_run_build, 3));
    TEST_ASSERT_EQUAL_INT(0, shell_rule_is_broad(git_status, 2));
    TEST_ASSERT_EQUAL_INT(0, shell_rule_is_broad(git, 1));
    TEST_ASSERT_EQUAL_INT(0, shell_rule_is_broad(docker_ps, 2));

    /* Naming what the wrapper runs doesn't narrow it: the entry still matches
     * any flags and operands that follow. */
    TEST_ASSERT_EQUAL_INT(1, shell_rule_is_broad(docker_run_alpine, 3));
    TEST_ASSERT_EQUAL_INT(1, shell_rule_is_broad(npm_exec_prettier, 3));
}

void test_brace_expansion_predicate(void) {
    TEST_ASSERT_EQUAL_INT(1, shell_token_has_brace_expansion(".{env,x}"));
    TEST_ASSERT_EQUAL_INT(1, shell_token_has_brace_expansion(".{a..c}"));
    TEST_ASSERT_EQUAL_INT(0, shell_token_has_brace_expansion("{print}"));
    TEST_ASSERT_EQUAL_INT(0, shell_token_has_brace_expansion("1{p}"));
    TEST_ASSERT_EQUAL_INT(0, shell_token_has_brace_expansion("plain"));
}

void test_status_reasons_and_labels(void) {
    TEST_ASSERT_EQUAL_STRING("writes to a file",
                             shell_rule_status_reason(SHELL_RULE_REDIRECT));
    TEST_ASSERT_TRUE(strlen(shell_rule_status_reason(SHELL_RULE_UNSAFE_SYNTAX)) > 0);
    TEST_ASSERT_TRUE(strlen(shell_rule_status_reason(SHELL_RULE_ALWAYS_ASK_FLAG)) > 0);
    TEST_ASSERT_TRUE(strlen(shell_rule_status_reason(SHELL_RULE_EXPANSION)) > 0);
    TEST_ASSERT_EQUAL_STRING("", shell_rule_status_reason(SHELL_RULE_OK));

    ShellRule rule;
    const char *detail = NULL;
    TEST_ASSERT_EQUAL_INT(SHELL_RULE_OK, rule_for("git show HEAD", &rule, &detail));
    char *label = shell_rule_label(&rule);
    TEST_ASSERT_EQUAL_STRING("\"git show HEAD\"", label);
    free(label);
    shell_rule_free(&rule);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_rule_is_the_whole_segment);
    RUN_TEST(test_rule_matches_only_the_same_tokens);
    RUN_TEST(test_expansions_are_unsaveable);
    RUN_TEST(test_always_ask_flags_block_rules);
    RUN_TEST(test_table_lookups_use_the_base_name);
    RUN_TEST(test_git_always_ask_additions);
    RUN_TEST(test_git_long_option_abbreviations_always_ask);
    RUN_TEST(test_destructive_flags_on_plain_commands_always_ask);
    RUN_TEST(test_always_ask_flag_scope);
    RUN_TEST(test_git_grep_pager_flags_always_ask);
    RUN_TEST(test_always_ask_spelling_variants);
    RUN_TEST(test_unsafe_syntax_and_redirects);
    RUN_TEST(test_broad_entries_are_recognised);
    RUN_TEST(test_brace_expansion_predicate);
    RUN_TEST(test_status_reasons_and_labels);
    return UNITY_END();
}
