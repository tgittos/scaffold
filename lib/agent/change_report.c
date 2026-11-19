#include "change_report.h"
#include "../util/process_spawn.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMMITS_BUF_SIZE  8192
#define STAT_BUF_SIZE     512

/* Run git with the given NULL-terminated args, capture stdout into buf.
 * Returns bytes written (>=0), or -1 on error. */
static int run_git(char **git_args, char *buf, size_t buf_size) {
    return process_spawn_capture(git_args, buf, buf_size);
}

/* Scan backwards from ptr (which must point to a keyword like " file") to
 * find the integer immediately preceding the leading space. */
static int extract_before_keyword(const char *line, const char *keyword) {
    const char *p = strstr(line, keyword);
    if (!p) return 0;

    /* p points to the space before the keyword; step back one to reach the
     * last character of the number (or another space if none). */
    if (p <= line) return 0;
    p--;  /* now on the last digit (or a comma/space) */

    /* skip any trailing punctuation that git may insert (e.g. comma in
     * "changed, 42 insertions" means we see ", " before the keyword) */
    while (p > line && !isdigit((unsigned char)*p)) p--;
    if (!isdigit((unsigned char)*p)) return 0;

    /* walk back to the first digit of the number */
    const char *start = p;
    while (start > line && isdigit((unsigned char)*(start - 1))) start--;

    int val = 0;
    sscanf(start, "%d", &val);
    return val;
}

/* Parse `git diff --shortstat` output into files/insertions/deletions. */
static void parse_shortstat(const char *line, int *files, int *ins, int *del) {
    *files = extract_before_keyword(line, " file");
    *ins   = extract_before_keyword(line, " insertion");
    *del   = extract_before_keyword(line, " deletion");
}

/* Count newline-terminated lines in a string. */
static int count_lines(const char *s) {
    int n = 0;
    for (; *s; s++) {
        if (*s == '\n') n++;
    }
    return n;
}

change_report_t *change_report_collect(const char *repo_root, const char *head_before) {
    if (!repo_root || !head_before || head_before[0] == '\0') return NULL;

    char commits_buf[COMMITS_BUF_SIZE];
    char range[128];

    /* Build "head_before..HEAD" range (head_before is a hex sha, safe). */
    snprintf(range, sizeof(range), "%s..HEAD", head_before);

    /* Get commits made since head_before. */
    char *log_args[] = {
        "git", "-C", (char *)repo_root, "log", range, "--oneline", NULL
    };
    int clen = run_git(log_args, commits_buf, sizeof(commits_buf));
    if (clen <= 0 || commits_buf[0] == '\0') return NULL;

    /* Get file-change statistics. */
    char stat_buf[STAT_BUF_SIZE] = {0};
    char *diff_args[] = {
        "git", "-C", (char *)repo_root, "diff", "--shortstat", range, NULL
    };
    run_git(diff_args, stat_buf, sizeof(stat_buf));

    change_report_t *r = calloc(1, sizeof(change_report_t));
    if (!r) return NULL;

    parse_shortstat(stat_buf, &r->files_changed, &r->insertions, &r->deletions);
    r->commit_count = count_lines(commits_buf);
    r->commits_text = strdup(commits_buf);
    if (!r->commits_text) {
        free(r);
        return NULL;
    }

    return r;
}

char *change_report_format(const change_report_t *report) {
    if (!report) return NULL;

    /* Estimate output size:
     *   "Changed: N files (+A −D)\n"  ~40 bytes
     *   per commit: "Commits: <7+> <subject>\n"  ~COMMITS_BUF_SIZE worst-case */
    size_t out_size = 64 + strlen(report->commits_text) + report->commit_count * 10 + 1;
    char *out = malloc(out_size);
    if (!out) return NULL;

    int pos = snprintf(out, out_size,
                       "Changed: %d %s (+%d \xe2\x88\x92%d)",
                       report->files_changed,
                       report->files_changed == 1 ? "file" : "files",
                       report->insertions,
                       report->deletions);
    if (pos < 0 || (size_t)pos >= out_size) { free(out); return NULL; }

    /* Append one "Commits: hash subject" line per commit. */
    const char *p = report->commits_text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t line_len = nl ? (size_t)(nl - p) : strlen(p);
        if (line_len == 0) { p = nl ? nl + 1 : p + strlen(p); continue; }

        int written = snprintf(out + pos, out_size - (size_t)pos,
                               "\nCommits: %.*s", (int)line_len, p);
        if (written < 0 || (size_t)(pos + written) >= out_size) break;
        pos += written;

        p = nl ? nl + 1 : p + line_len;
    }

    return out;
}

void change_report_free(change_report_t *report) {
    if (!report) return;
    free(report->commits_text);
    free(report);
}

char *change_report_build(const char *repo_root, const char *head_before) {
    change_report_t *r = change_report_collect(repo_root, head_before);
    if (!r) return NULL;
    char *text = change_report_format(r);
    change_report_free(r);
    return text;
}
