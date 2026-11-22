/* Test plugin whose on_load hook fails: it must be disabled with its own message. */
#include "reversa/plugin.h"
static int on_load(void* state) { (void)state; return 7; }
static const char* error(void* state) { (void)state; return "nope, not today"; }
static const rv_plugin plugin = {RV_API_VERSION, "failing-load", "1.0", "", 0, 0, on_load, 0, 0, 0, error};
RV_EXPORT const rv_plugin* reversa_plugin(void) { return &plugin; }
