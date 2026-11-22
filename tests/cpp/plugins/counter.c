/* Test plugin written in plain C: counts hooks and shows them in a view. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "reversa/plugin.h"

typedef struct {
    const rv_host* host;
    rv_program* program;
    int functions, renames, loaded;
    char last_rename[128];
    char text[256];
} counter;

static const char* render(rv_program* program, void* user) {
    counter* c = (counter*)user;
    (void)program;
    snprintf(c->text, sizeof c->text, "functions=%d renames=%d last=%s", c->functions, c->renames, c->last_rename);
    return c->text;
}

static void* create(const rv_host* host, rv_program* program) {
    counter* c = (counter*)calloc(1, sizeof(counter));
    c->host = host;
    c->program = program;
    host->add_view(program, "Counter", render, c);
    return c;
}
static void destroy(void* state) { free(state); }
static int on_load(void* state) { ((counter*)state)->loaded = 1; return 0; }
static int on_function_analyzed(void* state, rv_addr function) {
    counter* c = (counter*)state;
    rv_function_info info;
    if (c->host->function_info(c->program, function, &info) != 0) return 1;
    c->functions++;
    return 0;
}
static int on_rename(void* state, rv_addr addr, const char* old_name, const char* new_name) {
    counter* c = (counter*)state;
    (void)addr; (void)old_name;
    c->renames++;
    strncpy(c->last_rename, new_name, sizeof c->last_rename - 1);
    return 0;
}

static const rv_plugin plugin = {RV_API_VERSION, "counter", "1.0", "counts hooks (test)", create, destroy,
                                 on_load, on_function_analyzed, on_rename, NULL, NULL};
RV_EXPORT const rv_plugin* reversa_plugin(void) { return &plugin; }
