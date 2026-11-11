#include "executable_path.h"
#include <cosmo.h>
#include <string.h>

char* get_executable_path(void) {
    return strdup(GetProgramExecutableName());
}
