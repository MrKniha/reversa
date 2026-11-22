/* REVERSA plugin and script interface (stable C ABI).
 *
 * A plugin or a script is a shared library (.so on Linux, .dll on Windows) that exports one of:
 *
 *   const rv_plugin* reversa_plugin(void);                                  plugin: hooks + views
 *   int reversa_script(const rv_host* host, int argc, const char* const* argv);   script: one run
 *
 * The library talks to REVERSA only through the function table `rv_host`, so it needs no import
 * library and no REVERSA headers other than this file (and the optional C++ helpers in sdk.hpp).
 * Build it with any C or C++ compiler, for example:
 *
 *   g++ -std=c++17 -shared -fPIC -I<sdk>/include my_plugin.cpp -o my_plugin.so      (Linux)
 *   cl /std:c++17 /LD /EHsc /I<sdk>\include my_plugin.cpp                             (Windows)
 *
 * Rules
 * - Strings are UTF-8. A `const char*` returned by the host stays valid until the next host call
 *   on the same thread: copy it if you need it longer.
 * - Calls that can fail return 0 on success and -1 on error; host->last_error() explains why.
 * - Plugins run inside the REVERSA process and are NOT sandboxed: install only plugins you trust.
 *   A hook that reports an error (returns nonzero) disables only that plugin; a crash (for example
 *   a bad pointer) ends the whole application.
 * - Hooks may run on the analysis worker thread. The host serialises calls to one program.
 */
#ifndef REVERSA_PLUGIN_H
#define REVERSA_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RV_API_VERSION 1

#if defined(_WIN32)
#define RV_EXPORT __declspec(dllexport)
#else
#define RV_EXPORT __attribute__((visibility("default")))
#endif

typedef uint64_t rv_addr;
typedef struct rv_program rv_program; /* one opened and analysed binary, owned by the host */

typedef struct rv_function_info {
    rv_addr addr;
    uint64_t size;
    uint32_t insn_count;
    uint32_t block_count;
    int is_import; /* 1 = imported (code lives in another library) */
} rv_function_info;

typedef struct rv_xref {
    rv_addr from;
    rv_addr to;
    const char* kind; /* call jump read write addr data; static string, always valid */
} rv_xref;

typedef const char* (*rv_view_fn)(rv_program* program, void* user); /* returns text or Markdown */

typedef struct rv_host {
    uint32_t api_version; /* RV_API_VERSION of the host */
    uint32_t struct_size; /* sizeof(rv_host) of the host; new functions are only ever appended */

    const char* (*last_error)(void);
    void (*log)(rv_program* program, const char* message); /* printed on stdout, prefixed by nothing */

    /* scripts: open a file (binary or .rvproj), analysed; NULL on error. close() frees it. */
    rv_program* (*open)(const char* path);
    void (*close)(rv_program* program);
    int (*save)(rv_program* program, const char* project_path); /* NULL/"" = next to the binary */

    /* file */
    const char* (*path)(rv_program* program);
    const char* (*format)(rv_program* program); /* ELF PE RAW */
    const char* (*arch)(rv_program* program);   /* x86 x86-64 arm arm64 */
    int (*entry)(rv_program* program, rv_addr* out);
    int (*main_function)(rv_program* program, rv_addr* out);
    size_t (*read)(rv_program* program, rv_addr addr, void* buffer, size_t size); /* bytes copied */

    /* names */
    int (*resolve)(rv_program* program, const char* ref, rv_addr* out); /* "main", "0x401000", "strcmp" */
    const char* (*name_of)(rv_program* program, rv_addr addr);

    /* functions: index 0..function_count-1, sorted by address; imports included */
    size_t (*function_count)(rv_program* program);
    int (*function_at)(rv_program* program, size_t index, rv_function_info* out);
    int (*function_info)(rv_program* program, rv_addr addr, rv_function_info* out);
    /* list calls fill up to `capacity` entries and return the total count */
    size_t (*callers)(rv_program* program, rv_addr function, rv_addr* out, size_t capacity);
    size_t (*callees)(rv_program* program, rv_addr function, rv_addr* out, size_t capacity);
    size_t (*xrefs_to)(rv_program* program, rv_addr addr, rv_xref* out, size_t capacity);
    const char* (*pseudocode)(rv_program* program, rv_addr function);

    /* strings found by the analysis */
    size_t (*string_count)(rv_program* program);
    int (*string_at)(rv_program* program, size_t index, rv_addr* addr, const char** text);

    /* search; -1 (as size_t) on an invalid pattern */
    size_t (*search_bytes)(rv_program* program, const char* hex_pattern, rv_addr* out, size_t capacity);

    /* undoable annotations; an empty name/text removes it */
    int (*rename)(rv_program* program, rv_addr addr, const char* new_name);
    int (*set_comment)(rv_program* program, rv_addr addr, const char* text);
    int (*set_bookmark)(rv_program* program, rv_addr addr, const char* label);
    const char* (*comment)(rv_program* program, rv_addr addr);  /* NULL if none */
    const char* (*bookmark)(rv_program* program, rv_addr addr); /* NULL if none */

    /* custom view shown in the GUI (Views tab) and in reports; `user` is passed back to render */
    void (*add_view)(rv_program* program, const char* name, rv_view_fn render, void* user);
} rv_host;

/* Hooks return 0 on success; any other value disables the plugin (its message comes from error()). */
typedef struct rv_plugin {
    uint32_t api_version; /* RV_API_VERSION the plugin was built for */
    const char* name;
    const char* version;
    const char* description;
    void* (*create)(const rv_host* host, rv_program* program); /* per opened program; may return NULL */
    void (*destroy)(void* state);
    int (*on_load)(void* state);                         /* after loading, before analysis */
    int (*on_function_analyzed)(void* state, rv_addr function);
    int (*on_rename)(void* state, rv_addr addr, const char* old_name, const char* new_name);
    int (*on_unload)(void* state);
    const char* (*error)(void* state); /* message of the last failed hook */
} rv_plugin;

typedef const rv_plugin* (*rv_plugin_entry)(void);
typedef int (*rv_script_entry)(const rv_host* host, int argc, const char* const* argv);

#ifdef __cplusplus
}
#endif

#endif /* REVERSA_PLUGIN_H */
