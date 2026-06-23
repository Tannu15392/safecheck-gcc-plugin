/*
 * safecheck.c
 * Plugin core — GIMPLE pass registration and plugin_init()
 *
 * This file owns only the GCC plugin glue:
 *   - plugin_init()      : version check, arg parsing, pass registration
 *   - safecheck_pass     : GIMPLE pass class that dispatches all phases
 *
 * Analysis phases (separate translation units):
 *   unused.c / unused.h  — Phase 1:  Unused variable detection
 *   memory.c / memory.h  — Phase 2:  Memory leak & double-free detection
 *                          Phase 2b: Null pointer dereference detection
 *   uninit.c / uninit.h  — Phase 3:  Variable used before initialization
 *
 * Plugin arguments (passed via -fplugin-arg-safecheck-<key>=<val>):
 *   level=warn    emit only warnings (phases 1)
 *   level=strict  emit errors too    (phases 2, 2b, 3) [default]
 *
 * Build:
 *   make           → builds safecheck.so
 *   make test      → runs all test cases
 *
 * Usage:
 *   gcc-12 -fplugin=./safecheck.so file.c
 *   gcc-12 -fplugin=./safecheck.so -fplugin-arg-safecheck-level=warn file.c
 */

#include "gcc-plugin.h"
#include "plugin-version.h"
#include "tree.h"
#include "tree-pass.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "basic-block.h"
#include "context.h"
#include "diagnostic-core.h"
#include "unused.h"
#include "memory.h"
#include "uninit.h"
#include <stdio.h>
#include <string.h>

int plugin_is_GPL_compatible;

/* Severity level: warn = Phase 1 only, strict = all phases */
typedef enum { LEVEL_WARN = 0, LEVEL_STRICT = 1 } severity_t;
static severity_t g_level = LEVEL_STRICT;

/* ── GIMPLE pass definition ───────────────────────────────── */

static const pass_data safecheck_pass_data = {
    GIMPLE_PASS,             /* type                          */
    "safecheck",             /* name — shown in -fdump-passes */
    OPTGROUP_NONE,           /* optinfo_flags                 */
    TV_NONE,                 /* tv_id (timing)                */
    PROP_cfg | PROP_ssa,     /* properties_required           *
                              * We need the CFG (control flow  *
                              * graph) AND SSA form to be      *
                              * already built before we run.   */
    0,                       /* properties_provided           */
    0,                       /* properties_destroyed          */
    0,                       /* todo_flags_start              */
    0                        /* todo_flags_finish             */
};

/*
 * safecheck_pass — the GIMPLE optimisation pass object.
 *
 * GCC calls gate() first; if it returns true, calls execute().
 * We always want to run (gate returns true) and in execute() we
 * call each analysis phase in order.
 *
 * Phases run in this order so diagnostics appear roughly from
 * "structural" (unused vars) to "runtime danger" (null deref):
 *   1. check_unused_variables   — Phase 1 (unused.c)
 *   2. check_memory_management  — Phase 2 (memory.c)
 *   3. check_null_deref         — Phase 2b (memory.c)
 *   4. check_uninit_variables   — Phase 3 (uninit.c)
 */
class safecheck_pass : public gimple_opt_pass {
public:
    safecheck_pass(gcc::context *ctx)
        : gimple_opt_pass(safecheck_pass_data, ctx) {}

    bool gate(function *) override { return true; }

    unsigned int execute(function *fun) override {
        check_unused_variables(fun);   /* Phase 1  — unused.c  */
        check_memory_management(fun);  /* Phase 2  — memory.c  */
        check_null_deref(fun);         /* Phase 2b — memory.c  */
        check_uninit_variables(fun);   /* Phase 3  — uninit.c  */
        return 0;
    }
};

/* ── plugin_init ──────────────────────────────────────────── */

/*
 * plugin_init - called once when GCC loads the shared library.
 *
 * Steps:
 *   1. Version-check: ensure the plugin was compiled against the
 *      same GCC internals version currently running.
 *   2. Parse plugin arguments for the `level` key.
 *   3. Register the safecheck GIMPLE pass to run immediately after
 *      the "ssa" pass (when SSA + CFG are available but before
 *      GCC's own optimisation passes modify the IR).
 */
int plugin_init(struct plugin_name_args   *plugin_info,
                struct plugin_gcc_version *version)
{
    /* Step 1: version check */
    if (!plugin_default_version_check(version, &gcc_version)) {
        fprintf(stderr, "safecheck: GCC version mismatch — "
                        "recompile the plugin against this GCC\n");
        return 1;
    }

    /* Step 2: parse -fplugin-arg-safecheck-level=warn|strict */
    for (int i = 0; i < plugin_info->argc; i++) {
        const char *key = plugin_info->argv[i].key;
        const char *val = plugin_info->argv[i].value;
        if (val && !strcmp(key, "level")) {
            if      (!strcmp(val, "warn"))   g_level = LEVEL_WARN;
            else if (!strcmp(val, "strict")) g_level = LEVEL_STRICT;
            else
                fprintf(stderr,
                        "safecheck: unknown level='%s' (use warn|strict)\n",
                        val);
        }
    }

    fprintf(stderr,
            "safecheck: loaded — phases: unused | memory | null-deref | uninit "
            "[level=%s]\n",
            g_level == LEVEL_WARN ? "warn" : "strict");

    /* Step 3: insert pass after "ssa" pass */
    struct register_pass_info pass_info;
    pass_info.pass                     = new safecheck_pass(g);
    pass_info.reference_pass_name      = "ssa";
    pass_info.ref_pass_instance_number = 1;
    pass_info.pos_op                   = PASS_POS_INSERT_AFTER;

    register_callback(plugin_info->base_name,
                      PLUGIN_PASS_MANAGER_SETUP,
                      NULL,
                      &pass_info);
    return 0;
}
