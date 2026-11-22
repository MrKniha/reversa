/* Test plugin built for a future API version: must be reported as incompatible. */
#include "reversa/plugin.h"
static const rv_plugin plugin = {RV_API_VERSION + 98, "old-api", "1.0", "", 0, 0, 0, 0, 0, 0, 0};
RV_EXPORT const rv_plugin* reversa_plugin(void) { return &plugin; }
