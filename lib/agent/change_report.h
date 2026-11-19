#ifndef CHANGE_REPORT_H
#define CHANGE_REPORT_H

/**
 * Structured change report collected from a git repository.
 */
typedef struct {
    int files_changed;
    int insertions;
    int deletions;
    char *commits_text; /* malloc'd: newline-separated "hash subject" lines */
    int commit_count;
} change_report_t;

/**
 * Collect change data between head_before and HEAD in repo_root.
 *
 * @param repo_root   Absolute path to the git repository root.
 * @param head_before Git ref or hash captured before the agent ran.
 * @return Heap-allocated change_report_t, or NULL if nothing changed,
 *         not a git directory, or an error occurred. Caller calls change_report_free().
 */
change_report_t *change_report_collect(const char *repo_root, const char *head_before);

/**
 * Format a change_report_t as a human-readable multi-line string.
 *
 *   Changed: N files (+A −D)
 *   Commits: <hash> <subject>
 *   Commits: <hash> <subject>
 *
 * @return malloc'd string. Caller frees. NULL on allocation failure.
 */
char *change_report_format(const change_report_t *report);

/**
 * Free a change_report_t returned by change_report_collect().
 */
void change_report_free(change_report_t *report);

/**
 * Convenience facade: collect + format + free in one call.
 *
 * @param repo_root   Absolute path to the git repository root.
 * @param head_before Git ref or hash captured before the agent ran.
 * @return malloc'd formatted report string, or NULL if nothing changed
 *         or an error occurred. Caller frees.
 */
char *change_report_build(const char *repo_root, const char *head_before);

#endif /* CHANGE_REPORT_H */
