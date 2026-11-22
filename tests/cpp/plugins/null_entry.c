/* Test plugin whose entry point returns NULL. */
#include "reversa/plugin.h"
RV_EXPORT const rv_plugin* reversa_plugin(void) { return 0; }
