#ifndef EXECUTABLE_PATH_H
#define EXECUTABLE_PATH_H

/**
 * Get the path to the current ralph executable.
 *
 * Tries /proc/self/exe first (Linux). APE binaries run via an extracted
 * loader (e.g., /root/.ape-1.10), so if the path contains ".ape-" we
 * fall back to finding ralph in the current directory.
 *
 * @return Newly allocated path string (caller must free)
 */
char* get_executable_path(void);

#endif /* EXECUTABLE_PATH_H */
