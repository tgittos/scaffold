#ifndef EXECUTABLE_PATH_H
#define EXECUTABLE_PATH_H

/**
 * Get the absolute path to the running executable, not the APE loader
 * that may be executing it.
 *
 * @return Newly allocated path string (caller must free), or NULL on
 *         allocation failure
 */
char* get_executable_path(void);

#endif /* EXECUTABLE_PATH_H */
