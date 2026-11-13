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

static int always_ask(const char *command, int for_read_only) {
    ParsedShellCommand *p = parse(command);
    int ok = shell_has_always_ask_flag(p, for_read_only, NULL);
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
    assert_rule("make -j 4 -C build test", 0, 2, "make", "test");
    assert_rule("make CC=gcc all", 0, 2, "make", "all");
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
    assert_rule("grep -rn foo -- --output", 0, 1, "grep");
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

void test_always_ask_flag_read_only_scope(void) {
    TEST_ASSERT_EQUAL_INT(1, always_ask("find . -delete", 1));
    TEST_ASSERT_EQUAL_INT(1, always_ask("find . -delete", 0));
    TEST_ASSERT_EQUAL_INT(0, always_ask("git push -f", 1));
    TEST_ASSERT_EQUAL_INT(1, always_ask("git push -f", 0));
    TEST_ASSERT_EQUAL_INT(0, always_ask("find . -name '*.c'", 1));

    const char *flag = NULL;
    ParsedShellCommand *p = parse("find . -fprintf out");
    TEST_ASSERT_EQUAL_INT(1, shell_has_always_ask_flag(p, 1, &flag));
    TEST_ASSERT_EQUAL_STRING("-fprintf", flag);
    free_parsed_shell_command(p);
}

void test_status_reasons_and_labels(void) {
    TEST_ASSERT_EQUAL_STRING("writes to a file", shell_rule_status_reason(SHELL_RULE_REDIRECT));
    TEST_ASSERT_TRUE(strlen(shell_rule_status_reason(SHELL_RULE_WRAPPER)) > 0);
    TEST_ASSERT_TRUE(strlen(shell_rule_status_reason(SHELL_RULE_UNSAFE_SYNTAX)) > 0);
    TEST_ASSERT_TRUE(strlen(shell_rule_status_reason(SHELL_RULE_ALWAYS_ASK_FLAG)) > 0);
    TEST_ASSERT_TRUE(strlen(shell_rule_status_reason(SHELL_RULE_UNKNOWN_GLOBAL_OPT)) > 0);

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
    RUN_TEST(test_unknown_global_option_is_unsaveable);
    RUN_TEST(test_always_ask_flags_block_rules);
    RUN_TEST(test_tokens_after_double_dash_are_operands);
    RUN_TEST(test_never_broaden_commands_keep_two_token_rules);
    RUN_TEST(test_wrappers_are_unsaveable);
    RUN_TEST(test_unsafe_syntax_and_redirects);
    RUN_TEST(test_matching_ignores_flag_order_and_global_options);
    RUN_TEST(test_matching_exact_rules_uses_prefix_semantics);
    RUN_TEST(test_always_ask_flag_read_only_scope);
    RUN_TEST(test_status_reasons_and_labels);
    return UNITY_END();
}
