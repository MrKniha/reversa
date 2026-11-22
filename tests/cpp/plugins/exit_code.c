/* Test script: prints its arguments and returns exit code 3. */
#include <stdio.h>
#include "reversa/plugin.h"
RV_EXPORT int reversa_script(const rv_host* host, int argc, const char* const* argv) {
    int i;
    printf("api=%u argc=%d", host->api_version, argc);
    for (i = 1; i < argc; i++) printf(" %s", argv[i]);
    printf("\n");
    return 3;
}
